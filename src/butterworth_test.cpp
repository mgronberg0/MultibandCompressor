#include "butterworth.h"
#include <iostream>
#include <vector>
#include <cmath>
#include "AudioFile.h"

using namespace std;
using namespace butterworth;

// Test Compile:
// g++ -I"C:\Daisy_Projects\DaisySP\Source" -I"C:\Daisy_Projects\DaisySP\Source\Utility" -I"C:/Daisy_Projects/DaisyExamples/libDaisy/src" -o butterworth_test butterworth.cpp butterworth_test.cpp


int main()
{
    // Initialize the Butterworth filter
    Butterworth filter;
    filter.Init(48000.0f); // Assuming a sample rate of 44100 Hz

    //---------------------------------------------------------------
        std::cout << "**********************" << std::endl;
        std::cout << "Running Example: Load Audio File and Print Summary" << std::endl;
        std::cout << "**********************" << std::endl << std::endl;
        
        //---------------------------------------------------------------
        // 1. Set a file path to an audio file on your machine
        const std::string filePath = std::string "C:\python_dev\CCRMA-Experiments\chirp8Sec.wav";
        
        //---------------------------------------------------------------
        // 2. Create an AudioFile object and load the audio file
        
        AudioFile<float> a;
        bool loadedOK = a.load (filePath);
        
        /** If you hit this assert then the file path above
         probably doesn't refer to a valid audio file */
        assert (loadedOK);
        
        //---------------------------------------------------------------
        // 3. Let's print out some key details
        
        std::cout << "Bit Depth: " << a.getBitDepth() << std::endl;
        std::cout << "Sample Rate: " << a.getSampleRate() << std::endl;
        std::cout << "Num Channels: " << a.getNumChannels() << std::endl;
        std::cout << "Length in Seconds: " << a.getLengthInSeconds() << std::endl;
        std::cout << std::endl;

}