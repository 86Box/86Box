add_library(mt32emu STATIC Analog.cpp BReverbModel.cpp File.cpp FileStream.cpp
	LA32Ramp.cpp LA32FloatWaveGenerator.cpp LA32WaveGenerator.cpp
	MidiStreamParser.cpp Part.cpp Partial.cpp PartialManager.cpp
	Poly.cpp ROMInfo.cpp SampleRateConverter.cpp
	srchelper/srctools/src/FIRResampler.cpp
	srchelper/srctools/src/IIR2xResampler.cpp
	srchelper/srctools/src/LinearResampler.cpp
	srchelper/srctools/src/ResamplerModel.cpp
	srchelper/srctools/src/SincResampler.cpp
	srchelper/InternalResampler.cpp	Synth.cpp Tables.cpp TVA.cpp TVF.cpp
	TVP.cpp sha1/sha1.cpp c_interface/c_interface.cpp)