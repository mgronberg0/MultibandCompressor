/*  Stereo 3-band mastering compressor for the Electrosmith Daisy Field.

    Signal path:
        in -> per-channel input trim (global +/- balance) ->
        LR4 crossover (freq1) -> LOW
             \-> LR4 crossover (freq2) -> MID, HIGH
        LOW/MID/HIGH -> per-band stereo-linked compressor (mute/bypass aware)
        -> sum -> per-channel output trim (global +/- balance) ->
        safety clamp -> out

    See ANALYSIS.md for the original codebase audit, TESTING.md for DSP
    verification, OPERATIONS.md for build/flash/run instructions, and
    PHASE2_PLAN.md for the hardware-test feedback this revision addresses.

    ---- UI: three OLED pages, cycled by one keybed key -------------------
    HOME  - crossover frequencies, global input/output trim, and a
            per-band output-level readout (a glance at how loud each bus
            is, not an independent control).
    BAND  - the SELECTED band's threshold/ratio/attack/release and mute
            state.
    TRIM  - per-channel (L/R) input and output BALANCE: a +/- offset
            around the global trim on the Home page, not an independent
            level. Centered knob = no offset.

    A persistent meter strip (3 horizontal bars, one per band, with
    peak-hold) sits at the bottom of every page regardless of which page
    is showing. All three bars always show the same thing at once --
    Input level / Output level / Gain reduction -- cycled by SW_1.

    ---- Controls -----------------------------------------------------
    KNOB_1  Crossover 1 (Low/Mid split), ~60Hz  -> 1.5kHz, log
    KNOB_2  Crossover 2 (Mid/High split), ~300Hz -> 12kHz, log
            (clamped to stay >= 1.25x crossover 1)
    KNOB_3  Threshold   of the SELECTED band,  0dB  -> -48dB
    KNOB_4  Ratio       of the SELECTED band,  1:1  -> 20:1
    KNOB_5  Attack      of the SELECTED band,  1ms  -> 250ms, log
    KNOB_6  Release     of the SELECTED band, 20ms  -> 1s, log
    KNOB_7  Home/Band page: global input trim, -24dB -> +24dB
            Trim page:      input balance (L/R offset), -12dB -> +12dB
    KNOB_8  Home/Band page: global output trim, -24dB -> +24dB
            Trim page:      output balance (L/R offset), -12dB -> +12dB

    KEY_SELECT_LOW/MID/HIGH (keybed keys 0/1/2): select the band KNOB_3-6
        and the Band page apply to.
    KEY_MUTE_LOW/MID/HIGH   (keybed keys 3/4/5): toggle mute on that band
        directly. (No separate solo -- mute the other two instead.)
    KEY_PAGE                (keybed key 6): cycle Home -> Band -> Trim.
    Keybed keys 7-15 are unused/reserved for now.

    SW_1 short press: cycle meter mode (Input -> Output -> Gain
         Reduction -> back).
    SW_2 short press: toggle Bypass on the selected band (plays
         unprocessed, still audible/meterable).

    KNOB_3-6 and KNOB_7/8 all use a "pickup"/catch mode (see PickupKnob
    below): switching what a knob controls (band, or Home-vs-Trim page)
    never jumps that parameter to wherever the knob physically sits --
    it only takes live control again once moved back near where it was
    left for that context. Avoids yanking thresholds/levels around live.
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

enum Page
{
    PAGE_HOME,
    PAGE_BAND,
    PAGE_TRIM,
    PAGE_LAST,
};

enum MeterMode
{
    METER_INPUT,
    METER_OUTPUT,
    METER_GR,
    METER_LAST,
};

// Keybed key indices used by this project. daisy_field.cpp's keyboard scan
// reverses raw shift-register order within each 8-key half, but the B-row
// LED enum (LED_KEY_B1..B8) is declared in plain ascending order and lines
// up 1:1 with keyboard_state_ index 0..7 -- so by only using indices in
// that range, "LED index == key index" holds without needing to untangle
// the A-row's reversed enum ordering at all. Keys 7-15 are left free.
enum KeyIndex
{
    KEY_SELECT_LOW  = 0,
    KEY_SELECT_MID  = 1,
    KEY_SELECT_HIGH = 2,
    KEY_MUTE_LOW    = 3,
    KEY_MUTE_MID    = 4,
    KEY_MUTE_HIGH   = 5,
    KEY_PAGE        = 6,
};

const char *kBandNames[BAND_LAST]    = {"LOW", "MID", "HGH"};
const char *kMeterModeNames[METER_LAST] = {"IN", "OUT", "GR"};

inline float LinMap(float t, float lo, float hi) { return lo + t * (hi - lo); }
inline float LogMap(float t, float lo, float hi) { return lo * powf(hi / lo, t); }
inline float AmpToDb(float amp) { return 20.f * log10f(fmaxf(amp, 1e-6f)); }

float BlockPeak(const float *l, const float *r, size_t size)
{
    float peak = 0.f;
    for(size_t i = 0; i < size; i++)
    {
        peak = fmaxf(peak, fabsf(l[i]));
        peak = fmaxf(peak, fabsf(r[i]));
    }
    return peak;
}

/** Tracks a knob's value across N mutually-exclusive "contexts" (e.g. one
    per band, or Home-vs-Trim page) without letting it jump a parameter
    when the context changes. Each context remembers its own last-known
    raw (0..1) knob position; a context starts "live" only once the knob
    has been moved back within kCatchEps of that remembered position.
*/
template <int N>
struct PickupKnob
{
    float snapshot[N];
    bool  caught[N];      // true = currently live for this context
    bool  configured[N];  // true = this context has a real value worth protecting
    int   context = 0;

    void Init()
    {
        for(int i = 0; i < N; i++)
        {
            snapshot[i]   = 0.5f; // irrelevant until configured
            caught[i]     = true; // nothing to protect on a never-visited context --
            configured[i] = false; // live immediately, no blind sweep-to-0.5 required
        }
    }

    /** Switches which context this knob is pointed at. A context that's
        never been given a real value yet stays live immediately (there's
        nothing to jump away from); a context that already has one re-arms
        pickup so the knob has to be moved back to it before it's live
        again -- including on a return visit, not just the first departure.
    */
    void SetContext(int new_context)
    {
        if(new_context == context)
            return;
        context       = new_context;
        caught[context] = !configured[context];
    }

    /** Call once per tick with the knob's current raw value. Returns true
        (and updates the stored snapshot) if the knob is live for the
        current context this tick; otherwise leaves the real parameter
        alone.
    */
    bool Update(float raw)
    {
        constexpr float kCatchEps = 0.02f;
        if(!caught[context] && fabsf(raw - snapshot[context]) < kCatchEps)
            caught[context] = true;
        if(caught[context])
        {
            snapshot[context]   = raw;
            configured[context] = true;
            return true;
        }
        return false;
    }

    bool IsCaught() const { return caught[context]; }
};

} // namespace

static DaisyField hw;

// Per-channel crossover stages. xover1 splits at freq1_hz into (low, rest);
// xover2 splits that `rest` at freq2_hz into (mid, high). One instance per
// channel -- each holds its own filter state, so L and R never share history.
static LR4Crossover xover1[2];
static LR4Crossover xover2[2];

static BandCompressor bandComp[BAND_LAST];

// ---- Control-rate state (written in Controls(), read from the main loop) ----
static float freq1_hz        = 180.f;
static float freq2_hz        = 2500.f;
static float in_gain_db      = 0.f;
static float out_gain_db     = 0.f;
static float in_balance_db   = 0.f; // Trim page; + = Left louder
static float out_balance_db  = 0.f;
static int   selected_band   = BAND_LOW;
static Page  current_page    = PAGE_HOME;
static MeterMode meter_mode  = METER_GR;

static PickupKnob<BAND_LAST> pk_threshold, pk_ratio, pk_attack, pk_release;
static PickupKnob<2>         pk_in_gain, pk_out_gain; // context 0=global(Home/Band), 1=balance(Trim)

// Per-band metering: last block's peak amplitude pre- and post-compression,
// and a slowly-decaying peak-hold value (in dB, meaning depends on
// meter_mode) for the trailing marker on each meter bar.
static float band_in_peak[BAND_LAST]  = {0.f, 0.f, 0.f};
static float band_out_peak[BAND_LAST] = {0.f, 0.f, 0.f};
static float meter_peak_db[BAND_LAST] = {-100.f, -100.f, -100.f};
static uint32_t last_meter_update_ms  = 0;

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
    // Not hw.ProcessAllControls(): that also calls every knob's Process()
    // internally, and we call Process() on each knob ourselves below to
    // read the mapped value -- calling it twice per tick needlessly
    // double-advances each knob's smoothing filter. We don't use the CV
    // inputs ProcessAnalogControls() would also service, so only the
    // switches/keybed need the library's own update call.
    hw.ProcessDigitalControls();

    // --- Crossover points: always live, no pickup needed ---
    float t1 = hw.knob[DaisyField::KNOB_1].Process();
    float t2 = hw.knob[DaisyField::KNOB_2].Process();
    freq1_hz = LogMap(t1, 60.f, 1500.f);
    freq2_hz = fmaxf(LogMap(t2, 300.f, 12000.f), freq1_hz * 1.25f);
    for(int ch = 0; ch < 2; ch++)
    {
        xover1[ch].SetCutoff(freq1_hz);
        xover2[ch].SetCutoff(freq2_hz);
    }

    // --- Keybed: page cycle, band select, mute ---
    if(hw.KeyboardRisingEdge(KEY_PAGE))
        current_page = (Page)((current_page + 1) % PAGE_LAST);

    static const int kSelectKeys[BAND_LAST]
        = {KEY_SELECT_LOW, KEY_SELECT_MID, KEY_SELECT_HIGH};
    static const int kMuteKeys[BAND_LAST]
        = {KEY_MUTE_LOW, KEY_MUTE_MID, KEY_MUTE_HIGH};
    for(int b = 0; b < BAND_LAST; b++)
    {
        if(hw.KeyboardRisingEdge(kSelectKeys[b]))
            selected_band = b;
        if(hw.KeyboardRisingEdge(kMuteKeys[b]))
            bandComp[b].SetMuted(!bandComp[b].GetMuted());
    }

    // --- Per-band knobs (threshold/ratio/attack/release) w/ pickup ---
    pk_threshold.SetContext(selected_band);
    pk_ratio.SetContext(selected_band);
    pk_attack.SetContext(selected_band);
    pk_release.SetContext(selected_band);

    float raw3 = hw.knob[DaisyField::KNOB_3].Process();
    float raw4 = hw.knob[DaisyField::KNOB_4].Process();
    float raw5 = hw.knob[DaisyField::KNOB_5].Process();
    float raw6 = hw.knob[DaisyField::KNOB_6].Process();
    if(pk_threshold.Update(raw3)) ApplyBandKnob(selected_band, 0, raw3);
    if(pk_ratio.Update(raw4)) ApplyBandKnob(selected_band, 1, raw4);
    if(pk_attack.Update(raw5)) ApplyBandKnob(selected_band, 2, raw5);
    if(pk_release.Update(raw6)) ApplyBandKnob(selected_band, 3, raw6);

    // --- KNOB_7/8: global trim (Home/Band) vs. balance (Trim page) ---
    bool trim_page = (current_page == PAGE_TRIM);
    pk_in_gain.SetContext(trim_page ? 1 : 0);
    pk_out_gain.SetContext(trim_page ? 1 : 0);

    float raw7 = hw.knob[DaisyField::KNOB_7].Process();
    float raw8 = hw.knob[DaisyField::KNOB_8].Process();
    if(pk_in_gain.Update(raw7))
    {
        if(trim_page)
            in_balance_db = LinMap(raw7, -12.f, 12.f);
        else
            in_gain_db = LinMap(raw7, -24.f, 24.f);
    }
    if(pk_out_gain.Update(raw8))
    {
        if(trim_page)
            out_balance_db = LinMap(raw8, -12.f, 12.f);
        else
            out_gain_db = LinMap(raw8, -24.f, 24.f);
    }

    // --- SW_1: cycle meter mode. SW_2: toggle bypass on selected band ---
    if(hw.sw[DaisyField::SW_1].RisingEdge())
    {
        meter_mode = (MeterMode)((meter_mode + 1) % METER_LAST);
        for(int b = 0; b < BAND_LAST; b++)
            meter_peak_db[b] = -100.f; // avoid a stale peak from the old mode's range
    }
    if(hw.sw[DaisyField::SW_2].RisingEdge())
        bandComp[selected_band].SetBypassed(!bandComp[selected_band].GetBypassed());
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                           AudioHandle::OutputBuffer out,
                           size_t                    size)
{
    Controls();

    float in_gain_lin_l  = powf(10.f, (in_gain_db + in_balance_db) * 0.05f);
    float in_gain_lin_r  = powf(10.f, (in_gain_db - in_balance_db) * 0.05f);
    float out_gain_lin_l = powf(10.f, (out_gain_db + out_balance_db) * 0.05f);
    float out_gain_lin_r = powf(10.f, (out_gain_db - out_balance_db) * 0.05f);

    float lowL[kBlockSize], lowR[kBlockSize];
    float restL[kBlockSize], restR[kBlockSize];
    float midL[kBlockSize], midR[kBlockSize];
    float highL[kBlockSize], highR[kBlockSize];
    float outLowL[kBlockSize], outLowR[kBlockSize];
    float outMidL[kBlockSize], outMidR[kBlockSize];
    float outHighL[kBlockSize], outHighR[kBlockSize];

    for(size_t i = 0; i < size; i++)
    {
        float inL = in[0][i] * in_gain_lin_l;
        float inR = in[1][i] * in_gain_lin_r;

        xover1[0].Process(inL, &lowL[i], &restL[i]);
        xover1[1].Process(inR, &lowR[i], &restR[i]);

        xover2[0].Process(restL[i], &midL[i], &highL[i]);
        xover2[1].Process(restR[i], &midR[i], &highR[i]);
    }

    band_in_peak[BAND_LOW]  = BlockPeak(lowL, lowR, size);
    band_in_peak[BAND_MID]  = BlockPeak(midL, midR, size);
    band_in_peak[BAND_HIGH] = BlockPeak(highL, highR, size);

    bandComp[BAND_LOW].ProcessBlock(lowL, lowR, outLowL, outLowR, size);
    bandComp[BAND_MID].ProcessBlock(midL, midR, outMidL, outMidR, size);
    bandComp[BAND_HIGH].ProcessBlock(highL, highR, outHighL, outHighR, size);

    band_out_peak[BAND_LOW]  = BlockPeak(outLowL, outLowR, size);
    band_out_peak[BAND_MID]  = BlockPeak(outMidL, outMidR, size);
    band_out_peak[BAND_HIGH] = BlockPeak(outHighL, outHighR, size);

    for(size_t i = 0; i < size; i++)
    {
        float sumL = outLowL[i] + outMidL[i] + outHighL[i];
        float sumR = outLowR[i] + outMidR[i] + outHighR[i];

        // Cheap safety net, not a real limiter -- this is the end of a live
        // chain, a stray hot sum shouldn't be allowed to clip the DAC.
        out[0][i] = fclamp(sumL * out_gain_lin_l, -1.f, 1.f);
        out[1][i] = fclamp(sumR * out_gain_lin_r, -1.f, 1.f);
    }
}

/** Returns, for the current meter_mode, band b's current reading in dB
    (level modes: dBFS; GR mode: gain reduction, already <= 0 in dB). */
static float CurrentMeterDb(int b)
{
    switch(meter_mode)
    {
        case METER_INPUT: return AmpToDb(band_in_peak[b]);
        case METER_OUTPUT: return AmpToDb(band_out_peak[b]);
        case METER_GR:
        default: return bandComp[b].GetGainReductionDb();
    }
}

/** Maps a meter_mode-dependent dB reading to a 0..1 fill fraction. */
static float MeterDbToFrac(float db)
{
    if(meter_mode == METER_GR)
        return fclamp(-db, 0.f, 24.f) / 24.f;
    return fclamp((db + 48.f) / 48.f, 0.f, 1.f); // -48dBFS .. 0dBFS
}

static void UpdateDisplay()
{
    char line[24];

    hw.display.Fill(false);

    // --- Page-specific content (top of screen) ---
    switch(current_page)
    {
        case PAGE_HOME:
            // Deliberately no per-band dB readout here: the meter strip
            // below already shows each band's level, and a refresh that
            // fast makes a flickering text number unreadable anyway --
            // the continuously-moving bar reads fine at the same rate.
            snprintf(line, sizeof(line), "X1:%4.0f X2:%5.0fHz", freq1_hz, freq2_hz);
            hw.display.SetCursor(0, 0);
            hw.display.WriteString(line, Font_6x8, true);

            snprintf(line, sizeof(line), "IN:%+.0fdB OUT:%+.0fdB", in_gain_db, out_gain_db);
            hw.display.SetCursor(0, 9);
            hw.display.WriteString(line, Font_6x8, true);
            break;

        case PAGE_BAND:
        {
            char flag = bandComp[selected_band].GetMuted()
                            ? 'M'
                            : (bandComp[selected_band].GetBypassed() ? 'B' : ' ');
            snprintf(line, sizeof(line), "Band: %s %c", kBandNames[selected_band], flag);
            hw.display.SetCursor(0, 0);
            hw.display.WriteString(line, Font_6x8, true);

            // A trailing '~' marks a parameter whose knob hasn't been
            // picked up yet for this band (see PickupKnob) -- without
            // this, a knob that's waiting to catch looks identical to
            // "broken": turning it does nothing and there's no on-screen
            // sign of why.
            snprintf(line,
                      sizeof(line),
                      "T%+.0f%c R%.1f%c:1",
                      bandComp[selected_band].GetThreshold(),
                      pk_threshold.IsCaught() ? ' ' : '~',
                      bandComp[selected_band].GetRatio(),
                      pk_ratio.IsCaught() ? ' ' : '~');
            hw.display.SetCursor(0, 9);
            hw.display.WriteString(line, Font_6x8, true);

            snprintf(line,
                      sizeof(line),
                      "Atk%.0f%cms Rel%.0f%cms",
                      bandComp[selected_band].GetAttack() * 1000.f,
                      pk_attack.IsCaught() ? ' ' : '~',
                      bandComp[selected_band].GetRelease() * 1000.f,
                      pk_release.IsCaught() ? ' ' : '~');
            hw.display.SetCursor(0, 18);
            hw.display.WriteString(line, Font_6x8, true);
            break;
        }

        case PAGE_TRIM:
            hw.display.SetCursor(0, 0);
            hw.display.WriteString("Trim (L/R balance)", Font_6x8, true);

            snprintf(line, sizeof(line), "IN BAL:  %+.1fdB", in_balance_db);
            hw.display.SetCursor(0, 9);
            hw.display.WriteString(line, Font_6x8, true);

            snprintf(line, sizeof(line), "OUT BAL: %+.1fdB", out_balance_db);
            hw.display.SetCursor(0, 18);
            hw.display.WriteString(line, Font_6x8, true);
            break;

        default: break;
    }

    // --- Persistent meter strip (same on every page) ---
    snprintf(line, sizeof(line), "Meter: %s", kMeterModeNames[meter_mode]);
    hw.display.SetCursor(0, 27);
    hw.display.WriteString(line, Font_6x8, true);

    uint32_t now = System::GetNow();
    float    dt_sec
        = last_meter_update_ms == 0 ? 0.f : (now - last_meter_update_ms) / 1000.f;
    last_meter_update_ms = now;
    constexpr float kPeakFallDbPerSec = 20.f;

    static const int kBarX      = 8;
    static const int kBarWidth  = 118;
    static const int kBarHeight = 7;
    static const int kRowY[BAND_LAST] = {36, 45, 54};

    for(int b = 0; b < BAND_LAST; b++)
    {
        float db = CurrentMeterDb(b);
        if(db > meter_peak_db[b])
            meter_peak_db[b] = db;
        else
            meter_peak_db[b] -= kPeakFallDbPerSec * dt_sec;

        hw.display.SetCursor(0, kRowY[b]);
        hw.display.WriteString(kBandNames[b][0] == 'L' ? "L" : (kBandNames[b][0] == 'M' ? "M" : "H"),
                                 Font_6x8,
                                 true);

        int x1 = kBarX + kBarWidth;
        int y1 = kRowY[b] + kBarHeight;
        hw.display.DrawRect(kBarX, kRowY[b], x1, y1, true, false);

        float frac      = MeterDbToFrac(db);
        int   fill_width = (int)(frac * kBarWidth);
        if(fill_width > 0)
            hw.display.DrawRect(kBarX, kRowY[b], kBarX + fill_width, y1, true, true);

        float peak_frac = MeterDbToFrac(meter_peak_db[b]);
        int   peak_x     = kBarX + (int)(peak_frac * kBarWidth);
        hw.display.DrawLine(peak_x, kRowY[b], peak_x, y1, true);
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
    hw.led_driver.SetLed(kKnobLeds[0], pk_threshold.IsCaught() ? 1.f : 0.15f);
    hw.led_driver.SetLed(kKnobLeds[1], pk_ratio.IsCaught() ? 1.f : 0.15f);
    hw.led_driver.SetLed(kKnobLeds[2], pk_attack.IsCaught() ? 1.f : 0.15f);
    hw.led_driver.SetLed(kKnobLeds[3], pk_release.IsCaught() ? 1.f : 0.15f);

    static const int kSelectKeyLeds[BAND_LAST] = {
        DaisyField::LED_KEY_B1, // KEY_SELECT_LOW
        DaisyField::LED_KEY_B2, // KEY_SELECT_MID
        DaisyField::LED_KEY_B3, // KEY_SELECT_HIGH
    };
    static const int kMuteKeyLeds[BAND_LAST] = {
        DaisyField::LED_KEY_B4, // KEY_MUTE_LOW
        DaisyField::LED_KEY_B5, // KEY_MUTE_MID
        DaisyField::LED_KEY_B6, // KEY_MUTE_HIGH
    };
    for(int b = 0; b < BAND_LAST; b++)
    {
        hw.led_driver.SetLed(kSelectKeyLeds[b], b == selected_band ? 1.f : 0.1f);
        hw.led_driver.SetLed(kMuteKeyLeds[b], bandComp[b].GetMuted() ? 1.f : 0.1f);
    }
    hw.led_driver.SetLed(DaisyField::LED_KEY_B7, (current_page + 1) / (float)PAGE_LAST);

    hw.led_driver.SetLed(DaisyField::LED_SW_1, (meter_mode + 1) / (float)METER_LAST);
    hw.led_driver.SetLed(DaisyField::LED_SW_2,
                           bandComp[selected_band].GetBypassed() ? 1.f : 0.1f);

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

    // No need to seed band params from a blind guess: daisysp::Compressor's
    // own Init() already sets sane defaults (ratio 2:1, -12dB threshold,
    // 100ms attack/release), and PickupKnob now starts every context live,
    // so the very first knob read on each band overrides these immediately.
    for(int b = 0; b < BAND_LAST; b++)
        bandComp[b].Init(sample_rate);

    pk_threshold.Init();
    pk_ratio.Init();
    pk_attack.Init();
    pk_release.Init();
    pk_in_gain.Init();
    pk_out_gain.Init();

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        UpdateDisplay();
        UpdateLeds();
    }
}
