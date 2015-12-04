/*
 * Copyright (C) 2006-2013  Music Technology Group - Universitat Pompeu Fabra
 *
 * This file is part of Essentia
 *
 * Essentia is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation (FSF), either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the Affero GNU General Public License
 * version 3 along with this program.  If not, see http://www.gnu.org/licenses/
 */

#include "spsmodelanal.h"
#include "essentiamath.h"
#include <essentia/utils/synth_utils.h>

using namespace essentia;
using namespace standard;

const char* SpsModelAnal::name = "SpsModelAnal";
const char* SpsModelAnal::description = DOC("This algorithm computes the stochastic model analysis. \n"
"\n"
"It is recommended that the input \"spectrum\" be computed by the Spectrum algorithm. This algorithm uses SineModelAnal. See documentation for possible exceptions and input requirements on input \"spectrum\".\n"
"\n"
"References:\n"
"  https://github.com/MTG/sms-tools\n"
"  http://mtg.upf.edu/technologies/sms\n"
);



void SpsModelAnal::configure() {


  _window->configure( );

  _fft->configure( );


  _sineModelAnal->configure( "sampleRate", parameter("sampleRate").toReal(),
                              "maxnSines", parameter("maxnSines").toInt() ,
                              "freqDevOffset", parameter("freqDevOffset").toInt(),
                              "freqDevSlope",  parameter("freqDevSlope").toReal()
                              );

  int subtrFFTSize = std::min(512, 4*parameter("hopSize").toInt());  // make sure the FFT size is at least twice the hopsize
  _sineSubtraction->configure( "sampleRate", parameter("sampleRate").toReal(),
                              "fftSize", subtrFFTSize,
                              "hopSize", parameter("hopSize").toInt()
                              );

  // initialize array to accumulates two output frames from the sinesubtraction output
  _stocFrameIn.resize(2*parameter("hopSize").toInt());
  std::fill(_stocFrameIn.begin(), _stocFrameIn.end(), 0.);

  _stochasticModelAnal->configure( "sampleRate", parameter("sampleRate").toReal(),
                              "fftSize", 2*parameter("hopSize").toInt(),
                              "hopSize", parameter("hopSize").toInt(),
                              "stocf", parameter("stocf").toReal());


  _log.open("anal.log");
}



void SpsModelAnal::compute() {
  // inputs and outputs
  //const std::vector<std::complex<Real> >& fft = _fft.get();
  const std::vector<Real>& frame = _frame.get();

  std::vector<Real>& peakMagnitude = _magnitudes.get();
  std::vector<Real>& peakFrequency = _frequencies.get();
  std::vector<Real>& peakPhase = _phases.get();
  std::vector<Real>& stocEnv = _stocenv.get();

  std::vector<Real> wframe;
  std::vector<std::complex<Real> > fftin;
  std::vector<Real> fftmag;
  std::vector<Real> fftphase;


  _window->input("frame").set(frame);
  _window->output("frame").set(wframe);
  _window->compute();

  _fft->input("frame").set(wframe);
  _fft->output("fft").set(fftin);
  _fft->compute();

 _sineModelAnal->input("fft").set(fftin);
 _sineModelAnal->output("magnitudes").set(peakMagnitude);
 _sineModelAnal->output("frequencies").set(peakFrequency);
 _sineModelAnal->output("phases").set(peakPhase);

  _sineModelAnal->compute();


  std::vector<Real> subtrFrameOut;

// this needs to take into account overlap-add issues, introducing delay
 _sineSubtraction->input("frame").set(frame); // size is iput _fftSize
 _sineSubtraction->input("magnitudes").set(peakMagnitude);
 _sineSubtraction->input("frequencies").set(peakFrequency);
 _sineSubtraction->input("phases").set(peakPhase);
 _sineSubtraction->output("frame").set(subtrFrameOut); // Nsyn size
 _sineSubtraction->compute();

  updateStocInFrame(subtrFrameOut, _stocFrameIn); // shift and copy frame for stochastic model analysis

  _stochasticModelAnal->input("frame").set(_stocFrameIn);
  _stochasticModelAnal->output("stocenv").set(stocEnv);
  _stochasticModelAnal->compute();

// debug
  for (int i=0; i < (int) _stocFrameIn.size(); i++)
    _log << _stocFrameIn[i] << " ";
  _log << std::endl;

}


// ---------------------------
// additional methods

 // shift and copy frame for stochastic model analysis
void SpsModelAnal::updateStocInFrame(const std::vector<Real> frameIn, std::vector<Real> &frameAccumulator)
{
  for (int i =0; i < (int) frameIn.size(); ++i){
    if (i+ (int) frameIn.size() < (int) frameAccumulator.size()){
      frameAccumulator[i] = frameAccumulator[ i+ (int) frameIn.size()];
      frameAccumulator[i+ (int) frameIn.size()] = frameIn[i];
    }
  }
}

/*
void SpsModelAnal::stochasticModelAnalOld(const std::vector<std::complex<Real> > fftInput, const std::vector<Real> magnitudes, const std::vector<Real> frequencies, const std::vector<Real> phases, std::vector<Real> &stocEnv)
{

// TOD: refactor this function in two new essentia algorithms: sineSubctraction and sotchasticModelAnal

  // subtract sines
  std::vector<std::complex<Real> > fftSines;
  std::vector<std::complex<Real> > fftRes;

  _sineModelSynth->input("magnitudes").set(magnitudes);
  _sineModelSynth->input("frequencies").set(frequencies);
  _sineModelSynth->input("phases").set(phases);
  _sineModelSynth->output("fft").set(fftSines);

  _sineModelSynth->compute();

  fftRes = fftInput; // initialize output


  for (int i= 0; i < (int)fftRes.size(); ++i)
  {
    fftRes[i].real(fftInput[i].real() - fftSines[i].real());
    fftRes[i].imag(fftInput[i].imag() - fftSines[i].imag());
  }

  // the decimation factor must be in a range (0.01 and 1) Default 0 0.2
  Real stocf = std::min( std::max(0.01f, parameter("stocf").toReal()), 1.f);
  // To obtain the stochastic envelope, we resample only half of the FFT size (i.e. fftRes.size()-1)
  int stocSize =  int( stocf * parameter("fftSize").toInt() / 2.);
  stocSize += stocSize % 2; // make it even for FFT-based resample function. (Essentia FFT algorithms only accepts even size).


 // resampling to decimate residual envelope
 std::vector<Real> magResDB;
 Real mag, magdB;

 for (int i=0; i< (int) fftRes.size(); i++)
  {

    mag =  sqrt( fftRes[i].real() * fftRes[i].real() +  fftRes[i].imag() * fftRes[i].imag());
    magdB = std::max(-200., 20. * log10( mag + 1e-10));
    magResDB.push_back(magdB);

   // _log << magdB << " ";
  }
 // _log << std::endl;

if (stocf == 1.){
  stocEnv = magResDB;
  std::cout << "do not resample stocenv. size= " << stocEnv.size() << std::endl;
}
else{
  // magResDB needs to be of even size to use resample with essentia FFT algorithms.
  if ((magResDB.size() % 2) > 0)
    magResDB.erase(magResDB.end()); // remove last  idx = (N/2) +1
  resample(magResDB, stocEnv, stocSize);
}



// resampled envelope
for (int i=0; i< (int) stocEnv.size(); i++) {
Real magInput =       sqrt( fftInput[i].real() * fftInput[i].real() +  fftInput[i].imag() * fftInput[i].imag());
    Real magInputdB = std::max(-200., 20. * log10( magInput + 1e-10));

Real magSine =       sqrt( fftSines[i].real() * fftSines[i].real() +  fftSines[i].imag() * fftSines[i].imag());
    Real magSinedB = std::max(-200., 20. * log10( magSine + 1e-10));

Real magRes =       sqrt( fftRes[i].real() * fftRes[i].real() +  fftRes[i].imag() * fftRes[i].imag());
    Real magResdB = std::max(-200., 20. * log10( magRes + 1e-10));

 //_log << magSinedB << " " << magResdB << " " << magInputdB << " ";
 _log << fftSines[i].real() << " " << fftSines[i].imag() << " " << fftRes[i].real() << " " << fftRes[i].imag() << " " << fftInput[i].real() << " " << fftInput[i].imag() << " ";
 //_log << stocEnv[i] << " " ;
}
_log << std::endl;

}

*/

// Move this to new algorithm for ResampleFFT
//void SpsModelAnal::initializeFFT(std::vector<std::complex<Real> >&fft, int sizeFFT)
//{
//  fft.resize(sizeFFT);
//  for (int i=0; i < sizeFFT; ++i){
//    fft[i].real(0);
//    fft[i].imag(0);
//  }
//}

/*
// function to resample based on the FFT
// Use the same function than in python code
// http://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.resample.html
void SpsModelAnal::resample(const std::vector<Real> in, std::vector<Real> &out, const int sizeOut)
{

// TODO: consider adding this algorithhms as an essentia standard algorithm

  std::vector<std::complex<Real> >fftin; // temp vectors
  std::vector<std::complex<Real> >fftout; // temp vectors

  int sizeIn = (int) in.size();

  _fftres->input("frame").set(in);
  _fftres->output("fft").set(fftin);
  _fftres->compute();


  int hN = (sizeIn/2.)+1;
  int hNout = (sizeOut/2.)+1;
  initializeFFT(fftout, hNout);
  // fill positive spectrum to hN (upsampling zeros will be padded) or hNout (downsampling and high frequencies will be removed)
  for (int i = 0; i < std::min(hN, hNout); ++i)
  {
    // positive spectrums
    fftout[i].real( fftin[i].real());
    fftout[i].imag( fftin[i].imag());
  }

  _ifftres->input("fft").set(fftout);
  _ifftres->output("frame").set(out);

  _ifftres->compute();

  // normalize
  Real normalizationGain = 1. / float(sizeIn);
  for (int i = 0; i < sizeOut; ++i)
  {
   out[i] *= normalizationGain ;
  }

}

*/
