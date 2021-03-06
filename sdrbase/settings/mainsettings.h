#ifndef INCLUDE_SETTINGS_H
#define INCLUDE_SETTINGS_H

#include <QString>
#include "preferences.h"
#include "preset.h"
#include "audio/audiodeviceinfo.h"

class MainSettings {
public:
	MainSettings();
	~MainSettings();

	void load();
	void save() const;

	void resetToDefaults();

	Preset* newPreset(const QString& group, const QString& description);
	void deletePreset(const Preset* preset);
	int getPresetCount() const { return m_presets.count(); }
	const Preset* getPreset(int index) const { return m_presets[index]; }
	void sortPresets();

	Preset* getWorkingPreset() { return &m_workingPreset; }
	int getSourceIndex() const { return m_preferences.getSourceIndex(); }
	void setSourceIndex(int value) { m_preferences.setSourceIndex(value); }

	void setLatitude(float latitude) { m_preferences.setLatitude(latitude); }
	void setLongitude(float longitude) { m_preferences.setLongitude(longitude); }
	float getLatitude() const { return m_preferences.getLatitude(); }
	float getLongitude() const { return m_preferences.getLongitude(); }

	const AudioDeviceInfo *getAudioDeviceInfo() const { return m_audioDeviceInfo; }
	void setAudioDeviceInfo(AudioDeviceInfo *audioDeviceInfo) { m_audioDeviceInfo = audioDeviceInfo; }

protected:
	Preferences m_preferences;
	AudioDeviceInfo *m_audioDeviceInfo;
	Preset m_workingPreset;
	typedef QList<Preset*> Presets;
	Presets m_presets;
};

#endif // INCLUDE_SETTINGS_H
