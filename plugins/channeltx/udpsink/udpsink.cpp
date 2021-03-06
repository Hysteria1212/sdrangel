///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2017 Edouard Griffiths, F4EXB                                   //
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

#include <QDebug>

#include "dsp/upchannelizer.h"
#include "util/db.h"
#include "udpsinkmsg.h"
#include "udpsink.h"

MESSAGE_CLASS_DEFINITION(UDPSink::MsgUDPSinkConfigure, Message)
MESSAGE_CLASS_DEFINITION(UDPSink::MsgUDPSinkSpectrum, Message)
MESSAGE_CLASS_DEFINITION(UDPSink::MsgResetReadIndex, Message)

UDPSink::UDPSink(MessageQueue* uiMessageQueue, UDPSinkGUI* udpSinkGUI, BasebandSampleSink* spectrum) :
    m_uiMessageQueue(uiMessageQueue),
    m_udpSinkGUI(udpSinkGUI),
    m_spectrum(spectrum),
    m_spectrumEnabled(false),
    m_spectrumChunkSize(2160),
    m_spectrumChunkCounter(0),
    m_magsq(1e-10),
    m_movingAverage(16, 1e-10),
    m_inMovingAverage(480, 1e-10),
    m_sampleRateSum(0),
    m_sampleRateAvgCounter(0),
    m_levelCalcCount(0),
    m_peakLevel(0.0f),
    m_levelSum(0.0f),
    m_levelNbSamples(480),
    m_squelchOpen(false),
    m_squelchOpenCount(0),
    m_squelchCloseCount(0),
    m_squelchThreshold(4800),
    m_modPhasor(0.0f),
    m_SSBFilterBufferIndex(0),
    m_settingsMutex(QMutex::Recursive)
{
    setObjectName("UDPSink");
    m_udpHandler.setFeedbackMessageQueue(&m_inputMessageQueue);
    m_SSBFilter = new fftfilt(m_config.m_lowCutoff / m_config.m_inputSampleRate, m_config.m_rfBandwidth / m_config.m_inputSampleRate, m_ssbFftLen);
    m_SSBFilterBuffer = new Complex[m_ssbFftLen>>1]; // filter returns data exactly half of its size
    apply(true);
}

UDPSink::~UDPSink()
{
    delete[] m_SSBFilterBuffer;
    delete m_SSBFilter;
}

void UDPSink::start()
{
    m_udpHandler.start();
}

void UDPSink::stop()
{
    m_udpHandler.stop();
}

void UDPSink::pull(Sample& sample)
{
    if (m_running.m_channelMute)
    {
        sample.m_real = 0.0f;
        sample.m_imag = 0.0f;
        initSquelch(false);
        return;
    }

    Complex ci;

    m_settingsMutex.lock();

    if (m_interpolatorDistance > 1.0f) // decimate
    {
        modulateSample();

        while (!m_interpolator.decimate(&m_interpolatorDistanceRemain, m_modSample, &ci))
        {
            modulateSample();
        }
    }
    else
    {
        if (m_interpolator.interpolate(&m_interpolatorDistanceRemain, m_modSample, &ci))
        {
            modulateSample();
        }
    }

    m_interpolatorDistanceRemain += m_interpolatorDistance;

    ci *= m_carrierNco.nextIQ(); // shift to carrier frequency

    m_settingsMutex.unlock();

    double magsq = ci.real() * ci.real() + ci.imag() * ci.imag();
    magsq /= (1<<30);
    m_movingAverage.feed(magsq);
    m_magsq = m_movingAverage.average();

    sample.m_real = (FixReal) ci.real();
    sample.m_imag = (FixReal) ci.imag();
}

void UDPSink::modulateSample()
{
    if (m_running.m_sampleFormat == FormatS16LE) // Linear I/Q transponding
    {
        Sample s;

        m_udpHandler.readSample(s);

        uint64_t magsq = s.m_real * s.m_real + s.m_imag * s.m_imag;
        m_inMovingAverage.feed(magsq/1073741824.0);
        m_inMagsq = m_inMovingAverage.average();

        calculateSquelch(m_inMagsq);

        if (m_squelchOpen)
        {
            m_modSample.real(s.m_real * m_running.m_gainOut);
            m_modSample.imag(s.m_imag * m_running.m_gainOut);
            calculateLevel(m_modSample);
        }
        else
        {
            m_modSample.real(0.0f);
            m_modSample.imag(0.0f);
        }
    }
    else if (m_running.m_sampleFormat == FormatNFM)
    {
        FixReal t;
        readMonoSample(t);

        m_inMovingAverage.feed((t*t)/1073741824.0);
        m_inMagsq = m_inMovingAverage.average();

        calculateSquelch(m_inMagsq);

        if (m_squelchOpen)
        {
            m_modPhasor += (m_running.m_fmDeviation / m_running.m_inputSampleRate) * (t / 32768.0f) * M_PI * 2.0f;
            m_modSample.real(cos(m_modPhasor) * 10362.2f * m_running.m_gainOut);
            m_modSample.imag(sin(m_modPhasor) * 10362.2f * m_running.m_gainOut);
            calculateLevel(m_modSample);
        }
        else
        {
            m_modSample.real(0.0f);
            m_modSample.imag(0.0f);
        }
    }
    else if (m_running.m_sampleFormat == FormatAM)
    {
        FixReal t;
        readMonoSample(t);
        m_inMovingAverage.feed((t*t)/1073741824.0);
        m_inMagsq = m_inMovingAverage.average();

        calculateSquelch(m_inMagsq);

        if (m_squelchOpen)
        {
            m_modSample.real(((t / 32768.0f)*m_running.m_amModFactor*m_running.m_gainOut + 1.0f) * 16384.0f); // modulate and scale zero frequency carrier
            m_modSample.imag(0.0f);
            calculateLevel(m_modSample);
        }
        else
        {
            m_modSample.real(0.0f);
            m_modSample.imag(0.0f);
        }
    }
    else if ((m_running.m_sampleFormat == FormatLSB) || (m_running.m_sampleFormat == FormatUSB))
    {
        FixReal t;
        Complex c, ci;
        fftfilt::cmplx *filtered;
        int n_out = 0;

        readMonoSample(t);
        m_inMovingAverage.feed((t*t)/1073741824.0);
        m_inMagsq = m_inMovingAverage.average();

        calculateSquelch(m_inMagsq);

        if (m_squelchOpen)
        {
            ci.real((t / 32768.0f) * m_running.m_gainOut);
            ci.imag(0.0f);

            n_out = m_SSBFilter->runSSB(ci, &filtered, (m_running.m_sampleFormat == FormatUSB));

            if (n_out > 0)
            {
                memcpy((void *) m_SSBFilterBuffer, (const void *) filtered, n_out*sizeof(Complex));
                m_SSBFilterBufferIndex = 0;
            }

            c = m_SSBFilterBuffer[m_SSBFilterBufferIndex];
            m_modSample.real(m_SSBFilterBuffer[m_SSBFilterBufferIndex].real() * 32768.0f);
            m_modSample.imag(m_SSBFilterBuffer[m_SSBFilterBufferIndex].imag() * 32768.0f);
            m_SSBFilterBufferIndex++;

            calculateLevel(m_modSample);
        }
        else
        {
            m_modSample.real(0.0f);
            m_modSample.imag(0.0f);
        }
    }
    else
    {
        m_modSample.real(0.0f);
        m_modSample.imag(0.0f);
        initSquelch(false);
    }

    if (m_spectrum && m_spectrumEnabled && (m_spectrumChunkCounter < m_spectrumChunkSize - 1))
    {
        Sample s;
        s.m_real = (FixReal) m_modSample.real();
        s.m_imag = (FixReal) m_modSample.imag();
        m_sampleBuffer.push_back(s);
        m_spectrumChunkCounter++;
    }
    else
    {
        m_spectrum->feed(m_sampleBuffer.begin(), m_sampleBuffer.end(), false);
        m_sampleBuffer.clear();
        m_spectrumChunkCounter = 0;
    }
}

void UDPSink::calculateLevel(Real sample)
{
    if (m_levelCalcCount < m_levelNbSamples)
    {
        m_peakLevel = std::max(std::fabs(m_peakLevel), sample);
        m_levelSum += sample * sample;
        m_levelCalcCount++;
    }
    else
    {
        qreal rmsLevel = m_levelSum > 0.0 ? sqrt(m_levelSum / m_levelNbSamples) : 0.0;
        //qDebug("NFMMod::calculateLevel: %f %f", rmsLevel, m_peakLevel);
        emit levelChanged(rmsLevel, m_peakLevel, m_levelNbSamples);
        m_peakLevel = 0.0f;
        m_levelSum = 0.0f;
        m_levelCalcCount = 0;
    }
}

void UDPSink::calculateLevel(Complex sample)
{
    Real t = std::abs(sample);

    if (m_levelCalcCount < m_levelNbSamples)
    {
        m_peakLevel = std::max(std::fabs(m_peakLevel), t);
        m_levelSum += (t * t);
        m_levelCalcCount++;
    }
    else
    {
        qreal rmsLevel = m_levelSum > 0.0 ? sqrt((m_levelSum/(1<<30)) / m_levelNbSamples) : 0.0;
        emit levelChanged(rmsLevel, m_peakLevel / 32768.0, m_levelNbSamples);
        m_peakLevel = 0.0f;
        m_levelSum = 0.0f;
        m_levelCalcCount = 0;
    }
}

bool UDPSink::handleMessage(const Message& cmd)
{
    if (UpChannelizer::MsgChannelizerNotification::match(cmd))
    {
        UpChannelizer::MsgChannelizerNotification& notif = (UpChannelizer::MsgChannelizerNotification&) cmd;

        m_config.m_basebandSampleRate = notif.getBasebandSampleRate();
        m_config.m_outputSampleRate = notif.getSampleRate();
        m_config.m_inputFrequencyOffset = notif.getFrequencyOffset();

        apply(false);

        qDebug() << "UDPSink::handleMessage: MsgChannelizerNotification:"
                << " m_basebandSampleRate: " << m_config.m_basebandSampleRate
                << " m_outputSampleRate: " << m_config.m_outputSampleRate
                << " m_inputFrequencyOffset: " << m_config.m_inputFrequencyOffset;

        return true;
    }
    else if (MsgUDPSinkConfigure::match(cmd))
    {
        MsgUDPSinkConfigure& cfg = (MsgUDPSinkConfigure&) cmd;

        m_config.m_sampleFormat = cfg.getSampleFormat();
        m_config.m_inputSampleRate = cfg.getInputSampleRate();
        m_config.m_rfBandwidth = cfg.getRFBandwidth();
        m_config.m_fmDeviation = cfg.getFMDeviation();
        m_config.m_udpAddressStr = cfg.getUDPAddress();
        m_config.m_udpPort = cfg.getUDPPort();
        m_config.m_channelMute = cfg.getChannelMute();
        m_config.m_gainIn = cfg.getGainIn();
        m_config.m_gainOut = cfg.getGainOut();
        m_config.m_squelch = CalcDb::powerFromdB(cfg.getSquelchDB());
        m_config.m_squelchGate = cfg.getSquelchGate();
        m_config.m_squelchEnabled = cfg.getSquelchEnabled();
        m_config.m_autoRWBalance = cfg.getAutoRWBalance();
        m_config.m_stereoInput = cfg.getStereoInput();

        apply(cfg.getForce());

        qDebug() << "UDPSink::handleMessage: MsgUDPSinkConfigure:"
                << " m_sampleFormat: " << m_config.m_sampleFormat
                << " m_inputSampleRate: " << m_config.m_inputSampleRate
                << " m_rfBandwidth: " << m_config.m_rfBandwidth
                << " m_fmDeviation: " << m_config.m_fmDeviation
                << " m_udpAddressStr: " << m_config.m_udpAddressStr
                << " m_udpPort: " << m_config.m_udpPort
                << " m_channelMute: " << m_config.m_channelMute
                << " m_gainIn: " << m_config.m_gainIn
                << " m_gainOut: " << m_config.m_gainOut
                << " squelchDB: " << cfg.getSquelchDB()
                << " m_squelchGate: " << m_config.m_squelchGate
                << " m_squelch: " << m_config.m_squelch
                << " m_squelchEnabled: " << m_config.m_squelchEnabled
                << " m_autoRWBalance: " << m_config.m_autoRWBalance
                << " m_stereoInput: " << m_config.m_stereoInput;

        return true;
    }
    else if (UDPSinkMessages::MsgSampleRateCorrection::match(cmd))
    {
        UDPSinkMessages::MsgSampleRateCorrection& cfg = (UDPSinkMessages::MsgSampleRateCorrection&) cmd;
        Real newSampleRate = m_actualInputSampleRate + cfg.getCorrectionFactor() * m_actualInputSampleRate;

        // exclude values too way out nominal sample rate (20%)
        if ((newSampleRate < m_running.m_inputSampleRate * 1.2) && (newSampleRate >  m_running.m_inputSampleRate * 0.8))
        {
            m_actualInputSampleRate = newSampleRate;

            if ((cfg.getRawDeltaRatio() > -0.05) || (cfg.getRawDeltaRatio() < 0.05))
            {
                if (m_sampleRateAvgCounter < m_sampleRateAverageItems)
                {
                    m_sampleRateSum += m_actualInputSampleRate;
                    m_sampleRateAvgCounter++;
                }
            }
            else
            {
                m_sampleRateSum = 0.0;
                m_sampleRateAvgCounter = 0;
            }

            if (m_sampleRateAvgCounter == m_sampleRateAverageItems)
            {
                float avgRate = m_sampleRateSum / m_sampleRateAverageItems;
                qDebug("UDPSink::handleMessage: MsgSampleRateCorrection: corr: %+.6f new rate: %.0f: avg rate: %.0f",
                        cfg.getCorrectionFactor(),
                        m_actualInputSampleRate,
                        avgRate);
                m_actualInputSampleRate = avgRate;
                m_sampleRateSum = 0.0;
                m_sampleRateAvgCounter = 0;
            }
//            else
//            {
//                qDebug("UDPSink::handleMessage: MsgSampleRateCorrection: corr: %+.6f new rate: %.0f",
//                        cfg.getCorrectionFactor(),
//                        m_actualInputSampleRate);
//            }

            m_settingsMutex.lock();
            m_interpolatorDistanceRemain = 0;
            m_interpolatorConsumed = false;
            m_interpolatorDistance = (Real) m_actualInputSampleRate / (Real) m_config.m_outputSampleRate;
            //m_interpolator.create(48, m_actualInputSampleRate, m_config.m_rfBandwidth / 2.2, 3.0); // causes clicking: leaving at standard frequency
            m_settingsMutex.unlock();
        }

        return true;
    }
    else if (MsgUDPSinkSpectrum::match(cmd))
    {
        MsgUDPSinkSpectrum& spc = (MsgUDPSinkSpectrum&) cmd;
        m_spectrumEnabled = spc.getEnabled();
        qDebug() << "UDPSink::handleMessage: MsgUDPSinkSpectrum: m_spectrumEnabled: " << m_spectrumEnabled;

        return true;
    }
    else if (MsgResetReadIndex::match(cmd))
    {
        m_settingsMutex.lock();
        m_udpHandler.resetReadIndex();
        m_settingsMutex.unlock();

        qDebug() << "UDPSink::handleMessage: MsgResetReadIndex";

        return true;
    }
    else
    {
        if(m_spectrum != 0)
        {
           return m_spectrum->handleMessage(cmd);
        }
        else
        {
            return false;
        }
    }
}

void UDPSink::configure(MessageQueue* messageQueue,
        SampleFormat sampleFormat,
        Real outputSampleRate,
        Real rfBandwidth,
        int fmDeviation,
        Real amModFactor,
        const QString& udpAddress,
        int udpPort,
        bool channelMute,
        Real gainIn,
        Real gainOut,
        Real squelchDB,
        Real squelchGate,
        bool squelchEnabled,
        bool autoRWBalance,
        bool stereoInput,
        bool force)
{
    Message* cmd = MsgUDPSinkConfigure::create(sampleFormat,
            outputSampleRate,
            rfBandwidth,
            fmDeviation,
            amModFactor,
            udpAddress,
            udpPort,
            channelMute,
            gainIn,
            gainOut,
            squelchDB,
            squelchGate,
            squelchEnabled,
            autoRWBalance,
            stereoInput,
            force);
    messageQueue->push(cmd);
}

void UDPSink::setSpectrum(MessageQueue* messageQueue, bool enabled)
{
    Message* cmd = MsgUDPSinkSpectrum::create(enabled);
    messageQueue->push(cmd);
}

void UDPSink::resetReadIndex(MessageQueue* messageQueue)
{
    Message* cmd = MsgResetReadIndex::create();
    messageQueue->push(cmd);
}


void UDPSink::apply(bool force)
{
    if ((m_config.m_inputFrequencyOffset != m_running.m_inputFrequencyOffset) ||
        (m_config.m_outputSampleRate != m_running.m_outputSampleRate) || force)
    {
        m_settingsMutex.lock();
        m_carrierNco.setFreq(m_config.m_inputFrequencyOffset, m_config.m_outputSampleRate);
        m_settingsMutex.unlock();
    }

    if((m_config.m_outputSampleRate != m_running.m_outputSampleRate) ||
       (m_config.m_rfBandwidth != m_running.m_rfBandwidth) ||
       (m_config.m_inputSampleRate != m_running.m_inputSampleRate) || force)
    {
        m_settingsMutex.lock();
        m_interpolatorDistanceRemain = 0;
        m_interpolatorConsumed = false;
        m_interpolatorDistance = (Real) m_config.m_inputSampleRate / (Real) m_config.m_outputSampleRate;
        m_interpolator.create(48, m_config.m_inputSampleRate, m_config.m_rfBandwidth / 2.2, 3.0);
        m_actualInputSampleRate = m_config.m_inputSampleRate;
        m_udpHandler.resetReadIndex();
        m_sampleRateSum = 0.0;
        m_sampleRateAvgCounter = 0;
        m_spectrumChunkSize = m_config.m_inputSampleRate * 0.05; // 50 ms chunk
        m_spectrumChunkCounter = 0;
        m_levelNbSamples = m_config.m_inputSampleRate * 0.01; // every 10 ms
        m_levelCalcCount = 0;
        m_peakLevel = 0.0f;
        m_levelSum = 0.0f;
        m_udpHandler.resizeBuffer(m_config.m_inputSampleRate);
        m_inMovingAverage.resize(m_config.m_inputSampleRate * 0.01, 1e-10); // 10 ms
        m_squelchThreshold = m_config.m_inputSampleRate * m_config.m_squelchGate;
        initSquelch(m_squelchOpen);
        m_SSBFilter->create_filter(m_config.m_lowCutoff / m_config.m_inputSampleRate, m_config.m_rfBandwidth / m_config.m_inputSampleRate);
        m_settingsMutex.unlock();
    }

    if ((m_config.m_squelchGate != m_running.m_squelchGate) || force)
    {
        m_squelchThreshold = m_config.m_outputSampleRate * m_config.m_squelchGate;
        initSquelch(m_squelchOpen);
    }

    if ((m_config.m_udpAddressStr != m_running.m_udpAddressStr) ||
        (m_config.m_udpPort != m_running.m_udpPort) || force)
    {
        m_settingsMutex.lock();
        m_udpHandler.configureUDPLink(m_config.m_udpAddressStr, m_config.m_udpPort);
        m_settingsMutex.unlock();
    }

    if ((m_config.m_channelMute != m_running.m_channelMute) || force)
    {
        if (!m_config.m_channelMute) {
            m_udpHandler.resetReadIndex();
        }
    }

    if ((m_config.m_autoRWBalance != m_running.m_autoRWBalance) || force)
    {
        m_settingsMutex.lock();
        m_udpHandler.setAutoRWBalance(m_config.m_autoRWBalance);

        if (!m_config.m_autoRWBalance)
        {
            m_interpolatorDistanceRemain = 0;
            m_interpolatorConsumed = false;
            m_interpolatorDistance = (Real) m_config.m_inputSampleRate / (Real) m_config.m_outputSampleRate;
            m_interpolator.create(48, m_config.m_inputSampleRate, m_config.m_rfBandwidth / 2.2, 3.0);
            m_actualInputSampleRate = m_config.m_inputSampleRate;
            m_udpHandler.resetReadIndex();
        }

        m_settingsMutex.unlock();
    }

    m_running = m_config;
}
