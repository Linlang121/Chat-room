#pragma once

#include <fmod.hpp>
#include <fmod_errors.h>
#include <string>

#pragma comment(lib, "fmod_vc.lib")

class Sound {
public:
    Sound() : system_(nullptr) {}

    ~Sound() { release(); }

    bool init() {
        if (system_) return true;
        FMOD_RESULT r = FMOD::System_Create(&system_);
        if (r != FMOD_OK) return false;
        r = system_->init(512, FMOD_INIT_NORMAL, nullptr);
        if (r != FMOD_OK) {
            system_->release();
            system_ = nullptr;
            return false;
        }
        return true;
    }

    /* 播完再 release 否则没声 */
    bool playLikeAudiotest(const char* path, std::string* err = nullptr) {
        if (!system_) { if (err) *err = "FMOD system not inited"; return false; }
        if (!path || path[0] == '\0') { if (err) *err = "empty path"; return false; }
        releaseOneShotSound();
        FMOD::Sound* sound = nullptr;
        FMOD_RESULT result = system_->createSound(path, FMOD_DEFAULT, nullptr, &sound);
        if (result != FMOD_OK || !sound) {
            if (err) *err = std::string("createSound: ") + (const char*)FMOD_ErrorString(result);
            return false;
        }
        FMOD::Channel* channel = nullptr;
        system_->playSound(sound, nullptr, false, &channel);
        oneShotSound_ = sound;
        oneShotChannel_ = channel;
        return true;
    }

    void release() {
        releaseOneShotSound();
        if (system_) {
            system_->close();
            system_->release();
            system_ = nullptr;
        }
    }

    void update() {
        if (system_) {
            system_->update();
            releaseOneShotSoundIfFinished();
        }
    }

private:
    void releaseOneShotSound() {
        if (oneShotSound_) {
            oneShotSound_->release();
            oneShotSound_ = nullptr;
            oneShotChannel_ = nullptr;
        }
    }
    void releaseOneShotSoundIfFinished() {
        if (!oneShotChannel_ || !oneShotSound_) return;
        bool playing = false;
        if (oneShotChannel_->isPlaying(&playing) != FMOD_OK || !playing) {
            oneShotSound_->release();
            oneShotSound_ = nullptr;
            oneShotChannel_ = nullptr;
        }
    }

    FMOD::System* system_ = nullptr;
    FMOD::Sound* oneShotSound_ = nullptr;
    FMOD::Channel* oneShotChannel_ = nullptr;
};
