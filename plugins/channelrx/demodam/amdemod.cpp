///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2015 Edouard Griffiths, F4EXB.                                  //
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

#include "amdemod.h"

#include <QTime>
#include <QDebug>
#include <stdio.h>
#include <complex.h>
#include <dsp/downchannelizer.h>
#include "audio/audiooutput.h"
#include "dsp/dspengine.h"
#include "dsp/pidcontroller.h"

MESSAGE_CLASS_DEFINITION(AMDemod::MsgConfigureAMDemod, Message)

AMDemod::AMDemod() :
    m_squelchOpen(false),
	m_audioFifo(4, 48000),
	m_settingsMutex(QMutex::Recursive)
{
	setObjectName("AMDemod");

	m_config.m_inputSampleRate = 96000;
	m_config.m_inputFrequencyOffset = 0;
	m_config.m_rfBandwidth = 12500;
	m_config.m_afBandwidth = 3000;
	m_config.m_squelch = -40.0;
	m_config.m_volume = 2.0;
	m_config.m_audioSampleRate = DSPEngine::instance()->getAudioSampleRate();

	apply();

	m_audioBuffer.resize(1<<14);
	m_audioBufferFill = 0;

	m_movingAverage.resize(16, 0);
	m_volumeAGC.resize(4096, 0.003, 0);
	m_magsq = 0.0;

	DSPEngine::instance()->addAudioSink(&m_audioFifo);
}

AMDemod::~AMDemod()
{
	DSPEngine::instance()->removeAudioSink(&m_audioFifo);
}

void AMDemod::configure(MessageQueue* messageQueue, Real rfBandwidth, Real afBandwidth, Real volume, Real squelch, bool audioMute)
{
	Message* cmd = MsgConfigureAMDemod::create(rfBandwidth, afBandwidth, volume, squelch, audioMute);
	messageQueue->push(cmd);
}

void AMDemod::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool firstOfBurst)
{
	Complex ci;

	m_settingsMutex.lock();

	for (SampleVector::const_iterator it = begin; it != end; ++it)
	{
		//Complex c(it->real() / 32768.0, it->imag() / 32768.0);
		Complex c(it->real(), it->imag());
		c *= m_nco.nextIQ();

		if (m_interpolator.decimate(&m_interpolatorDistanceRemain, c, &ci))
		{
			//m_sampleBuffer.push_back(Sample(ci.real() * 32767.0, ci.imag() * 32767.0));
			m_sampleBuffer.push_back(Sample(ci.real(), ci.imag()));

			Real magsq = ci.real() * ci.real() + ci.imag() * ci.imag();
			magsq /= (1<<30);
			m_movingAverage.feed(magsq);
			m_magsq = m_movingAverage.average();

			if (m_magsq >= m_squelchLevel)
			{
				if (m_squelchCount <= m_running.m_audioSampleRate / 10)
				{
					m_squelchCount++;
				}
			}
			else
			{
				if (m_squelchCount > 1)
				{
					m_squelchCount -= 2;
				}
			}

			qint16 sample;

			if ((m_squelchCount >= m_running.m_audioSampleRate / 20) && !m_running.m_audioMute)
			{
				Real demod = sqrt(magsq);

				demod = m_lowpass.filter(demod);

				if (demod < -1)
				{
					demod = -1;
				}
				else if (demod > 1)
				{
					demod = 1;
				}

				m_volumeAGC.feed(demod);

				Real attack = (m_squelchCount - (m_running.m_audioSampleRate / 20)) / (Real) (m_running.m_audioSampleRate / 20);
				demod *= ((0.003 * attack) / m_volumeAGC.getValue());
				demod *= m_running.m_volume;
				sample = demod * 32700 * 16;
				m_squelchOpen = true;
			}
			else
			{
				sample = 0;
				m_squelchOpen = false;
			}

			m_audioBuffer[m_audioBufferFill].l = sample;
			m_audioBuffer[m_audioBufferFill].r = sample;
			++m_audioBufferFill;

			if (m_audioBufferFill >= m_audioBuffer.size())
			{
				uint res = m_audioFifo.write((const quint8*)&m_audioBuffer[0], m_audioBufferFill, 10);

				if (res != m_audioBufferFill)
				{
					qDebug("AMDemod::feed: %u/%u audio samples written", res, m_audioBufferFill);
				}

				m_audioBufferFill = 0;
			}

			m_interpolatorDistanceRemain += m_interpolatorDistance;
		}
	}

	if (m_audioBufferFill > 0)
	{
		uint res = m_audioFifo.write((const quint8*)&m_audioBuffer[0], m_audioBufferFill, 10);

		if (res != m_audioBufferFill)
		{
			qDebug("AMDemod::feed: %u/%u tail samples written", res, m_audioBufferFill);
		}

		m_audioBufferFill = 0;
	}

	m_sampleBuffer.clear();

	m_settingsMutex.unlock();
}

void AMDemod::start()
{
	qDebug() << "AMDemod::start: m_inputSampleRate: " << m_config.m_inputSampleRate
			<< " m_inputFrequencyOffset: " << m_config.m_inputFrequencyOffset;

	m_squelchCount = 0;
	m_audioFifo.clear();
}

void AMDemod::stop()
{
}

bool AMDemod::handleMessage(const Message& cmd)
{
	qDebug() << "AMDemod::handleMessage";

	if (DownChannelizer::MsgChannelizerNotification::match(cmd))
	{
		DownChannelizer::MsgChannelizerNotification& notif = (DownChannelizer::MsgChannelizerNotification&) cmd;

		m_config.m_inputSampleRate = notif.getSampleRate();
		m_config.m_inputFrequencyOffset = notif.getFrequencyOffset();

		apply();

		qDebug() << "AMDemod::handleMessage: MsgChannelizerNotification:"
				<< " m_inputSampleRate: " << m_config.m_inputSampleRate
				<< " m_inputFrequencyOffset: " << m_config.m_inputFrequencyOffset;

		return true;
	}
	else if (MsgConfigureAMDemod::match(cmd))
	{
		MsgConfigureAMDemod& cfg = (MsgConfigureAMDemod&) cmd;

		m_config.m_rfBandwidth = cfg.getRFBandwidth();
		m_config.m_afBandwidth = cfg.getAFBandwidth();
		m_config.m_volume = cfg.getVolume();
		m_config.m_squelch = cfg.getSquelch();
		m_config.m_audioMute = cfg.getAudioMute();

		apply();

		qDebug() << "AMDemod::handleMessage: MsgConfigureAMDemod:"
				<< " m_rfBandwidth: " << m_config.m_rfBandwidth
				<< " m_afBandwidth: " << m_config.m_afBandwidth
				<< " m_volume: " << m_config.m_volume
				<< " m_squelch: " << m_config.m_squelch
				<< " m_audioMute: " << m_config.m_audioMute;

		return true;
	}
	else
	{
		return false;
	}
}

void AMDemod::apply()
{

	if((m_config.m_inputFrequencyOffset != m_running.m_inputFrequencyOffset) ||
		(m_config.m_inputSampleRate != m_running.m_inputSampleRate))
	{
		m_nco.setFreq(-m_config.m_inputFrequencyOffset, m_config.m_inputSampleRate);
	}

	if((m_config.m_inputSampleRate != m_running.m_inputSampleRate) ||
		(m_config.m_rfBandwidth != m_running.m_rfBandwidth))
	{
		m_settingsMutex.lock();
		m_interpolator.create(16, m_config.m_inputSampleRate, m_config.m_rfBandwidth / 2.2);
		m_interpolatorDistanceRemain = 0;
		m_interpolatorDistance = (Real) m_config.m_inputSampleRate / (Real) m_config.m_audioSampleRate;
		m_settingsMutex.unlock();
	}

	if((m_config.m_afBandwidth != m_running.m_afBandwidth) ||
		(m_config.m_audioSampleRate != m_running.m_audioSampleRate))
	{
		m_settingsMutex.lock();
		m_lowpass.create(21, m_config.m_audioSampleRate, m_config.m_afBandwidth);
		m_settingsMutex.unlock();
	}

	if(m_config.m_squelch != m_running.m_squelch)
	{
		m_squelchLevel = pow(10.0, m_config.m_squelch / 20.0);
		m_squelchLevel *= m_squelchLevel;
	}

	m_running.m_inputSampleRate = m_config.m_inputSampleRate;
	m_running.m_inputFrequencyOffset = m_config.m_inputFrequencyOffset;
	m_running.m_rfBandwidth = m_config.m_rfBandwidth;
	m_running.m_afBandwidth = m_config.m_afBandwidth;
	m_running.m_squelch = m_config.m_squelch;
	m_running.m_volume = m_config.m_volume;
	m_running.m_audioSampleRate = m_config.m_audioSampleRate;
	m_running.m_audioMute = m_config.m_audioMute;
}
