/* This file is part of RetroFE.
 *
 * RetroFE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RetroFE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RetroFE.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "Component.h"
#include <SDL/SDL.h>
#include <cstddef>
#include <string>

// A looping frame sequence, meant for animated backgrounds (<animatedImage>).
//
// The frames come from one headerless file of same-sized raw RGB565 (little
// endian) frames stored back to back, which is what ffmpeg writes with
// "-f rawvideo -pix_fmt rgb565le". The file is memory-mapped read-only, so the
// frames sit in the kernel page cache rather than on the RetroFE heap, and
// each frame is blitted straight from the mapping.
//
// The animation only advances at its own frame rate, and only asks the page
// for a redraw when the visible frame changes. After a while without input it
// drops to its idle frame rate (0 = paused, so nothing is redrawn at all).
// freeGraphicsMemory(), which RetroFE calls before launching a game, unmaps
// the file and drops it from the page cache.
class AnimatedImage : public Component
{
public:
    AnimatedImage(std::string file, std::string altFile, int frameWidth, int frameHeight,
                  float fps, float idleFps, bool loop, bool useColorKey, Uint32 colorKey,
                  Page &p, float scaleX, float scaleY);
    virtual ~AnimatedImage();

    void freeGraphicsMemory();
    void allocateGraphicsMemory();
    void update(float dt);
    void draw();
    bool mustRender();

    // Settings shared by every animated image (from settings.conf)
    static void configure(bool enabled, float maxFps, float idleTimeout);
    // Resets the idle timer; RetroFE calls this on user input
    static void notifyUserActivity();
    // Bytes currently mapped by all animated images, for diagnostics
    static size_t mappedBytes();

private:
    bool  mapFile(const std::string &path);
    void  unmapFile();
    float currentFps() const;

    std::string  file_;
    std::string  altFile_;
    int          frameWidth_;
    int          frameHeight_;
    size_t       frameBytes_;
    int          frameCount_;
    float        fps_;
    float        idleFps_;
    bool         loop_;
    bool         useColorKey_;
    Uint32       colorKey_;
    float        scaleX_;
    float        scaleY_;

    int          fd_;
    void        *map_;
    size_t       mapLength_;
    SDL_Surface *surface_;       // RGB565 surface whose pixels point into the mapping

    double       phase_;         // position in the sequence, in frames
    int          currentFrame_;
    int          drawnFrame_;    // frame shown by the last draw(), -1 if none yet
    bool         warnedScaling_;

    static bool   enabled_;
    static float  maxFps_;
    static float  idleTimeout_;
    static Uint32 lastActivityTicks_;
    static size_t mappedBytes_;
};
