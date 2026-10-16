/*  Stereo 3-band mastering compressor for the Electrosmith Daisy Field.

    Signal path:
        in -> input trim -> LR4 crossover (freq1) -> LOW
                                  \-> LR4 crossover (freq2) -> MID, HIGH
        LOW/MID/HIGH -> per-band stereo-linked compressor (mute/bypass aware)
        -> sum -> output trim -> safety clamp -> out

    See ANALYSIS.md for how this relates to the original hand-written
    version and why it's built this way (hardware mismatch + the
    exp2()/K*K Butterworth bug in particular).

    Control layout (Daisy Field: 8 knobs, 2 switches, OLED):
        KNOB_1  Crossover 1 (Low/Mid split), ~60Hz  -> 1.5kHz, log
        KNOB_2  Crossover 2 (Mid/High split), ~300Hz -> 12kHz, log
                (clamped to stay >= 1.25x crossover 1)
        KNOB_3  Threshold   of the SELECTED band,  0dB  -> -48dB
        KNOB_4  Ratio       of the SELECTED band,  1:1  -> 20:1
        KNOB_5  Attack      of the SELECTED band,  1ms  -> 250ms, log
        KNOB_6  Release     of the SELECTED band, 20ms  -> 1s, log
        KNOB_7  Input trim,  -24dB -> +24dB
        KNOB_8  Output trim, -24dB -> +24dB

        SW_1  short press: cycle selected band (Low -> Mid -> High)
        SW_2  short press: toggle Mute   on the selected band
        SW_2  long press (>=500ms): toggle Bypass on the selected band
              (bypass = band plays unprocessed, still audible/meterable;
               mute+bypassed bands combined let you solo a band by
               muting the other two)

    KNOB_3..6 use a "pickup"/catch mode per band: when you switch bands,
    those four knobs don't jump the new band's parameters to wherever
    the knob currently sits. Each knob only takes control again once
    you've moved it back near the value it was at when you last left
    that band -- avoids yanking thresholds/ratios around live.
*/

#include <cmath>
#include <cstdio>
#include "daisy_field.h"
#include "daisysp.h"
#include "src/crossover.h"
#include "src/compressor.h"

using namespace daisy;
using namespace daisysp;
using namespace multiband;

namespace
{
constexpr size_t kBlockSize = 48;

enum BandIndex
{
    BAND_LOW,
    BAND_MID,
    BAND_HIGH,
    BAND_LAST,
};

const char *kBandNames[BAND_LAST] = {"LOW", "MID", "HGH"};

inline float LinMap(float t, float lo, float hi) { return lo + t * (hi - lo); }
inline float LogMap(float t, float lo, float hi) { return lo * powf(hi / lo, t); }

} // namespace

static DaisyField hw;

// Per-channel crossover stages. xover1 splits at freq1_hz into (low, rest);
// xover2 splits that `rest` at freq2_hz into (mid, high). One instance per
// channel -- each holds its own filter state, so L and R never share history.
static LR4Crossover xover1[2];
static LR4Crossover xover2[2];

static BandCompressor bandComp[BAND_LAST];

// ---- Control-rate state (written in Controls(), read from the main loop) ----
static float freq1_hz    = 180.f;
static float freq2_hz    = 2500.f;
static float in_gain_db  = 0.f;
static float out_gain_db = 0.f;
static int   selected_band = BAND_LOW;

// Pickup/catch state for the 4 per-band knobs (threshold, ratio, attack, release)
static bool  knob_caught[4];
static float band_knob_snapshot[BAND_LAST][4];
static float knob_raw_live[4]; // for LED feedback in the main loop

// SW_2 short-press (mute) vs. long-press (bypass) detection
static uint32_t sw2_held_ticks     = 0;
static uint32_t long_press_ticks   = 500;
static bool     sw2_longpress_fired = false;

static void ApplyBandKnob(int band, int knob_idx, float raw)
{
    switch(knob_idx)
    {
        case 0: bandComp[band].SetThreshold(LinMap(raw, 0.f, -48.f)); break;
        case 1: bandComp[band].SetRatio(1.f + powf(raw, 1.5f) * 19.f); break;
        case 2: bandComp[band].SetAttack(LogMap(raw, 0.001f, 0.25f)); break;
        case 3: bandComp[band].SetRelease(LogMap(raw, 0.02f, 1.0f)); break;
    }
}

static void Controls()
{
    hw.ProcessAllControls();

    // --- Global, always-live parameters ---
    float t1    = hw.knob[DaisyField::KNOB_1].Process();
    float t2    = hw.knob[DaisyField::KNOB_2].Process();
    float t_in  = hw.knob[DaisyField::KNOB_7].Process();
    float t_out = hw.knob[DaisyField::KNOB_8].Process();

    freq1_hz    = LogMap(t1, 60.f, 1500.f);
    freq2_hz    = fmaxf(LogMap(t2, 300.f, 12000.f), freq1_hz * 1.25f);
    in_gain_db  = LinMap(t_in, -24.f, 24.f);
    out_gain_db = LinMap(t_out, -24.f, 24.f);

    for(int ch = 0; ch < 2; ch++)
    {
        xover1[ch].SetCutoff(freq1_hz);
        xover2[ch].SetCutoff(freq2_hz);
    }

    // --- Per-band knobs (threshold/ratio/attack/release) w/ pickup ---
    float raw[4] = {
        hw.knob[DaisyField::KNOB_3].Process(),
        hw.knob[DaisyField::KNOB_4].Process(),
        hw.knob[DaisyField::KNOB_5].Process(),
        hw.knob[DaisyField::KNOB_6].Process(),
    };
    for(int k = 0; k < 4; k++)
        knob_raw_live[k] = raw[k];

    // --- Band select (SW_1) ---
    if(hw.sw[DaisyField::SW_1].RisingEdge())
    {
        selected_band = (selected_band + 1) % BAND_LAST;
        static const float kCatchEps = 0.02f;
        for(int k = 0; k < 4; k++)
        {
            // Re-arm pickup for the newly-selected band, unless the knob
            // already happens to sit right where that band left off.
            knob_caught[k]
                = fabsf(raw[k] - band_knob_snapshot[selected_band][k]) < kCatchEps;
        }
    }

    static const float kCatchEps = 0.02f;
    for(int k = 0; k < 4; k++)
    {
        if(!knob_caught[k]
           && fabsf(raw[k] - band_knob_snapshot[selected_band][k]) < kCatchEps)
            knob_caught[k] = true;

        if(knob_caught[k])
        {
            band_knob_snapshot[selected_band][k] = raw[k];
            ApplyBandKnob(selected_band, k, raw[k]);
        }
    }

    // --- Mute (short press) / Bypass (long press) on SW_2 ---
    if(hw.sw[DaisyField::SW_2].Pressed())
    {
        sw2_held_ticks++;
        if(!sw2_longpress_fired && sw2_held_ticks >= long_press_ticks)
        {
            bandComp[selected_band].SetBypassed(!bandComp[selected_band].GetBypassed());
            sw2_longpress_fired = true;
        }
    }
    else
    {
        sw2_held_ticks = 0;
    }

    if(hw.sw[DaisyField::SW_2].FallingEdge())
    {
        if(!sw2_longpress_fired)
            bandComp[selected_band].SetMuted(!bandComp[selected_band].GetMuted());
        sw2_longpress_fired = false;
    }
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                           AudioHandle::OutputBuffer out,
                           size_t                    size)
{
    Controls();

    float in_gain_lin  = powf(10.f, in_gain_db * 0.05f);
    float out_gain_lin = powf(10.f, out_gain_db * 0.05f);

    float lowL[kBlockSize], lowR[kBlockSize];
    float restL[kBlockSize], restR[kBlockSize];
    float midL[kBlockSize], midR[kBlockSize];
    float highL[kBlockSize], highR[kBlockSize];
    float outLowL[kBlockSize], outLowR[kBlockSize];
    float outMidL[kBlockSize], outMidR[kBlockSize];
    float outHighL[kBlockSize], outHighR[kBlockSize];

    for(size_t i = 0; i < size; i++)
    {
        float inL = in[0][i] * in_gain_lin;
        float inR = in[1][i] * in_gain_lin;

        xover1[0].Process(inL, &lowL[i], &restL[i]);
        xover1[1].Process(inR, &lowR[i], &restR[i]);

        xover2[0].Process(restL[i], &midL[i], &highL[i]);
        xover2[1].Process(restR[i], &midR[i], &highR[i]);
    }

    bandComp[BAND_LOW].ProcessBlock(lowL, lowR, outLowL, outLowR, size);
    bandComp[BAND_MID].ProcessBlock(midL, midR, outMidL, outMidR, size);
    bandComp[BAND_HIGH].ProcessBlock(highL, highR, outHighL, outHighR, size);

    for(size_t i = 0; i < size; i++)
    {
        float sumL = outLowL[i] + outMidL[i] + outHighL[i];
        float sumR = outLowR[i] + outMidR[i] + outHighR[i];

        // Cheap safety net, not a real limiter -- this is the end of a live
        // chain, a stray hot sum shouldn't be allowed to clip the DAC.
        out[0][i] = fclamp(sumL * out_gain_lin, -1.f, 1.f);
        out[1][i] = fclamp(sumR * out_gain_lin, -1.f, 1.f);
    }
}

static void UpdateDisplay()
{
    char line[24];

    hw.display.Fill(false);

    snprintf(line, sizeof(line), "X1:%4.0f X2:%5.0fHz", freq1_hz, freq2_hz);
    hw.display.SetCursor(0, 0);
    hw.display.WriteString(line, Font_6x8, true);

    snprintf(line, sizeof(line), "IN:%+.0fdB OUT:%+.0fdB", in_gain_db, out_gain_db);
    hw.display.SetCursor(0, 9);
    hw.display.WriteString(line, Font_6x8, true);

    static const int kColX[BAND_LAST] = {2, 46, 90};
    static const int kColW             = 36;

    for(int b = 0; b < BAND_LAST; b++)
    {
        char flag = bandComp[b].GetMuted() ? 'M' : (bandComp[b].GetBypassed() ? 'B' : ' ');
        snprintf(line, sizeof(line), "%c%s%c", b == selected_band ? '>' : ' ',
                  kBandNames[b], flag);
        hw.display.SetCursor(kColX[b], 20);
        hw.display.WriteString(line, Font_6x8, true);
    }

    snprintf(line,
              sizeof(line),
              "T%+.0f R%.1f A%.0f Rl%.0f",
              bandComp[selected_band].GetThreshold(),
              bandComp[selected_band].GetRatio(),
              bandComp[selected_band].GetAttack() * 1000.f,
              bandComp[selected_band].GetRelease() * 1000.f);
    hw.display.SetCursor(0, 29);
    hw.display.WriteString(line, Font_6x8, true);

    // Gain-reduction meters: one filled bar per band, growing downward
    // from y=40 as more compression is applied (0 .. -24dB range).
    const int kMeterTop    = 40;
    const int kMeterBottom = 62;
    const int kMeterHeight = kMeterBottom - kMeterTop;

    for(int b = 0; b < BAND_LAST; b++)
    {
        int x0 = kColX[b];
        int x1 = x0 + kColW;

        hw.display.DrawRect(x0, kMeterTop, x1, kMeterBottom, true, false);

        float gr_db = fclamp(-bandComp[b].GetGainReductionDb(), 0.f, 24.f);
        int   fill_h = (int)(gr_db / 24.f * kMeterHeight);
        if(fill_h > 0)
            hw.display.DrawRect(
                x0, kMeterBottom - fill_h, x1, kMeterBottom, true, true);
    }

    hw.display.Update();
}

static void UpdateLeds()
{
    static const int kKnobLeds[4] = {
        DaisyField::LED_KNOB_3,
        DaisyField::LED_KNOB_4,
        DaisyField::LED_KNOB_5,
        DaisyField::LED_KNOB_6,
    };

    for(int k = 0; k < 4; k++)
        hw.led_driver.SetLed(kKnobLeds[k], knob_caught[k] ? 1.f : 0.15f);

    hw.led_driver.SetLed(DaisyField::LED_SW_1, (selected_band + 1) / 3.f);

    float sw2_led = bandComp[selected_band].GetMuted()
                        ? 1.f
                        : (bandComp[selected_band].GetBypassed() ? 0.5f : 0.05f);
    hw.led_driver.SetLed(DaisyField::LED_SW_2, sw2_led);

    hw.led_driver.SwapBuffersAndTransmit();
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    float sample_rate = hw.AudioSampleRate();

    for(int ch = 0; ch < 2; ch++)
    {
        xover1[ch].Init(sample_rate);
        xover2[ch].Init(sample_rate);
        xover1[ch].SetCutoff(freq1_hz);
        xover2[ch].SetCutoff(freq2_hz);
    }

    for(int b = 0; b < BAND_LAST; b++)
    {
        bandComp[b].Init(sample_rate);
        for(int k = 0; k < 4; k++)
        {
            band_knob_snapshot[b][k] = 0.5f;
            ApplyBandKnob(b, k, 0.5f);
        }
    }
    for(int k = 0; k < 4; k++)
        knob_caught[k] = false; // require pickup on every knob at boot

    long_press_ticks = (uint32_t)(0.5f * hw.AudioCallbackRate());

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        UpdateDisplay();
        UpdateLeds();
    }
}
