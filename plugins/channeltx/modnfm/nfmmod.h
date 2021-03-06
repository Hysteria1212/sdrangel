///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#ifndef PLUGINS_CHANNELTX_MODNFM_NFMMOD_H_
#define PLUGINS_CHANNELTX_MODNFM_NFMMOD_H_

#include <QMutex>
#include <vector>
#include <iostream>
#include <fstream>

#include "dsp/basebandsamplesource.h"
#include "dsp/nco.h"
#include "dsp/ncof.h"
#include "dsp/interpolator.h"
#include "dsp/lowpass.h"
#include "dsp/bandpass.h"
#include "dsp/movingaverage.h"
#include "dsp/agc.h"
#include "dsp/cwkeyer.h"
#include "audio/audiofifo.h"
#include "util/message.h"

class NFMMod : public BasebandSampleSource {
    Q_OBJECT

public:
    typedef enum
    {
        NFMModInputNone,
        NFMModInputTone,
        NFMModInputFile,
        NFMModInputAudio,
        NFMModInputCWTone
    } NFMModInputAF;

    class MsgConfigureFileSourceName : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        const QString& getFileName() const { return m_fileName; }

        static MsgConfigureFileSourceName* create(const QString& fileName)
        {
            return new MsgConfigureFileSourceName(fileName);
        }

    private:
        QString m_fileName;

        MsgConfigureFileSourceName(const QString& fileName) :
            Message(),
            m_fileName(fileName)
        { }
    };

    class MsgConfigureFileSourceSeek : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        int getPercentage() const { return m_seekPercentage; }

        static MsgConfigureFileSourceSeek* create(int seekPercentage)
        {
            return new MsgConfigureFileSourceSeek(seekPercentage);
        }

    protected:
        int m_seekPercentage; //!< percentage of seek position from the beginning 0..100

        MsgConfigureFileSourceSeek(int seekPercentage) :
            Message(),
            m_seekPercentage(seekPercentage)
        { }
    };

    class MsgConfigureFileSourceStreamTiming : public Message {
        MESSAGE_CLASS_DECLARATION

    public:

        static MsgConfigureFileSourceStreamTiming* create()
        {
            return new MsgConfigureFileSourceStreamTiming();
        }

    private:

        MsgConfigureFileSourceStreamTiming() :
            Message()
        { }
    };

    class MsgConfigureAFInput : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        NFMModInputAF getAFInput() const { return m_afInput; }

        static MsgConfigureAFInput* create(NFMModInputAF afInput)
        {
            return new MsgConfigureAFInput(afInput);
        }

    private:
        NFMModInputAF m_afInput;

        MsgConfigureAFInput(NFMModInputAF afInput) :
            Message(),
            m_afInput(afInput)
        { }
    };

    class MsgReportFileSourceStreamTiming : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        std::size_t getSamplesCount() const { return m_samplesCount; }

        static MsgReportFileSourceStreamTiming* create(std::size_t samplesCount)
        {
            return new MsgReportFileSourceStreamTiming(samplesCount);
        }

    protected:
        std::size_t m_samplesCount;

        MsgReportFileSourceStreamTiming(std::size_t samplesCount) :
            Message(),
            m_samplesCount(samplesCount)
        { }
    };

    class MsgReportFileSourceStreamData : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        int getSampleRate() const { return m_sampleRate; }
        quint32 getRecordLength() const { return m_recordLength; }

        static MsgReportFileSourceStreamData* create(int sampleRate,
                quint32 recordLength)
        {
            return new MsgReportFileSourceStreamData(sampleRate, recordLength);
        }

    protected:
        int m_sampleRate;
        quint32 m_recordLength;

        MsgReportFileSourceStreamData(int sampleRate,
                quint32 recordLength) :
            Message(),
            m_sampleRate(sampleRate),
            m_recordLength(recordLength)
        { }
    };

    //=================================================================

    NFMMod();
    ~NFMMod();

    void configure(MessageQueue* messageQueue,
            Real rfBandwidth,
            Real afBandwidth,
            float fmDeviation,
            float toneFrequency,
			float volumeFactor,
            bool channelMute,
            bool playLoop,
            bool ctcssOn,
            float ctcssFrequency);

    virtual void pull(Sample& sample);
    virtual void pullAudio(int nbSamples);
    virtual void start();
    virtual void stop();
    virtual bool handleMessage(const Message& cmd);

    double getMagSq() const { return m_magsq; }

    CWKeyer *getCWKeyer() { return &m_cwKeyer; }

signals:
    /**
     * Level changed
     * \param rmsLevel RMS level in range 0.0 - 1.0
     * \param peakLevel Peak level in range 0.0 - 1.0
     * \param numSamples Number of audio samples analyzed
     */
    void levelChanged(qreal rmsLevel, qreal peakLevel, int numSamples);


private:
    class MsgConfigureNFMMod : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        Real getRFBandwidth() const { return m_rfBandwidth; }
        Real getAFBandwidth() const { return m_afBandwidth; }
        float getFMDeviation() const { return m_fmDeviation; }
        float getToneFrequency() const { return m_toneFrequency; }
        float getVolumeFactor() const { return m_volumeFactor; }
        bool getChannelMute() const { return m_channelMute; }
        bool getPlayLoop() const { return m_playLoop; }
        bool getCTCSSOn() const { return m_ctcssOn; }
        float getCTCSSFrequency() const { return m_ctcssFrequency; }

        static MsgConfigureNFMMod* create(Real rfBandwidth,
                Real afBandwidth,
                float fmDeviation,
                float toneFrequency,
                float volumeFactor,
                bool channelMute,
                bool playLoop,
                bool ctcssOn,
                float ctcssFrequency)
        {
            return new MsgConfigureNFMMod(rfBandwidth,
                    afBandwidth,
                    fmDeviation,
                    toneFrequency,
                    volumeFactor,
                    channelMute,
                    playLoop,
                    ctcssOn,
                    ctcssFrequency);
        }

    private:
        Real m_rfBandwidth;
        Real m_afBandwidth;
        float m_fmDeviation;
        float m_toneFrequency;
        float m_volumeFactor;
        bool m_channelMute;
        bool m_playLoop;
        bool m_ctcssOn;
        float m_ctcssFrequency;

        MsgConfigureNFMMod(Real rfBandwidth,
                Real afBandwidth,
                float fmDeviation,
                float toneFrequency,
                float volumeFactor,
                bool channelMute,
                bool playLoop,
                bool ctcssOn,
                float ctcssFrequency) :
            Message(),
            m_rfBandwidth(rfBandwidth),
            m_afBandwidth(afBandwidth),
            m_fmDeviation(fmDeviation),
            m_toneFrequency(toneFrequency),
            m_volumeFactor(volumeFactor),
            m_channelMute(channelMute),
			m_playLoop(playLoop),
			m_ctcssOn(ctcssOn),
			m_ctcssFrequency(ctcssFrequency)
        { }
    };

    //=================================================================

    enum RateState {
        RSInitialFill,
        RSRunning
    };

    struct Config {
        int m_basebandSampleRate;
        int m_outputSampleRate;
        qint64 m_inputFrequencyOffset;
        Real m_rfBandwidth;
        Real m_afBandwidth;
        float m_fmDeviation;
        float m_toneFrequency;
        float m_volumeFactor;
        quint32 m_audioSampleRate;
        bool m_channelMute;
        bool m_playLoop;
        bool m_ctcssOn;
        float m_ctcssFrequency;

        Config() :
            m_basebandSampleRate(0),
            m_outputSampleRate(-1),
            m_inputFrequencyOffset(0),
            m_rfBandwidth(-1),
            m_afBandwidth(-1),
            m_fmDeviation(5000.0f),
            m_toneFrequency(1000.0f),
            m_volumeFactor(1.0f),
            m_audioSampleRate(0),
            m_channelMute(false),
			m_playLoop(false),
			m_ctcssOn(false),
			m_ctcssFrequency(88.5)
        { }
    };

    //=================================================================

    Config m_config;
    Config m_running;

    NCO m_carrierNco;
    NCOF m_toneNco;
    NCOF m_ctcssNco;
    float m_modPhasor; //!< baseband modulator phasor
    Complex m_modSample;
    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;
    bool m_interpolatorConsumed;
    Lowpass<Real> m_lowpass;
    Bandpass<Real> m_bandpass;

    double m_magsq;
    MovingAverage<double> m_movingAverage;
    SimpleAGC m_volumeAGC;

    AudioVector m_audioBuffer;
    uint m_audioBufferFill;

    AudioFifo m_audioFifo;
    SampleVector m_sampleBuffer;
    QMutex m_settingsMutex;

    std::ifstream m_ifstream;
    QString m_fileName;
    quint64 m_fileSize;     //!< raw file size (bytes)
    quint32 m_recordLength; //!< record length in seconds computed from file size
    int m_sampleRate;

    NFMModInputAF m_afInput;
    quint32 m_levelCalcCount;
    Real m_peakLevel;
    Real m_levelSum;
    CWKeyer m_cwKeyer;
    CWSmoother m_cwSmoother;
    static const int m_levelNbSamples;

    void apply();
    void pullAF(Real& sample);
    void calculateLevel(Real& sample);
    void modulateSample();
    void openFileStream();
    void seekFileStream(int seekPercentage);
};


#endif /* PLUGINS_CHANNELTX_MODNFM_NFMMOD_H_ */
