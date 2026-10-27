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
#include "AnimatedImage.h"
#include "../ViewInfo.h"
#include "../../SDL.h"
#include "../../Utility/Log.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

bool   AnimatedImage::enabled_           = true;
float  AnimatedImage::maxFps_            = 30.f;
float  AnimatedImage::idleTimeout_       = 30.f;
Uint32 AnimatedImage::lastActivityTicks_ = 0;
size_t AnimatedImage::frameFileBytes_    = 0;


AnimatedImage::AnimatedImage(std::string file, std::string altFile, int frameWidth, int frameHeight,
                             float fps, float idleFps, bool loop, bool useColorKey, Uint32 colorKey,
                             Page &p, float scaleX, float scaleY)
    : Component(p)
    , file_(file)
    , altFile_(altFile)
    , frameWidth_(frameWidth)
    , frameHeight_(frameHeight)
    , frameBytes_(0)
    , frameCount_(0)
    , fps_(fps)
    , idleFps_(idleFps)
    , loop_(loop)
    , useColorKey_(useColorKey)
    , colorKey_(colorKey)
    , scaleX_(scaleX)
    , scaleY_(scaleY)
    , fileBytes_(0)
    , frame_(NULL)
    , loadedFrame_(-1)
    , warnedRead_(false)
    , surface_(NULL)
    , phase_(0)
    , currentFrame_(0)
    , drawnFrame_(-1)
    , warnedScaling_(false)
{
    allocateGraphicsMemory();
}


AnimatedImage::~AnimatedImage()
{
    freeGraphicsMemory();
}


void AnimatedImage::configure(bool enabled, float maxFps, float idleTimeout)
{
    enabled_           = enabled;
    maxFps_            = (maxFps > 0) ? maxFps : 0;
    idleTimeout_       = (idleTimeout > 0) ? idleTimeout : 0;
    lastActivityTicks_ = SDL_GetTicks();
}


void AnimatedImage::notifyUserActivity()
{
    lastActivityTicks_ = SDL_GetTicks();
}


size_t AnimatedImage::frameFileBytes()
{
    return frameFileBytes_;
}


// Checks that path holds at least one frame, and makes it the file in use
bool AnimatedImage::openFile(const std::string &path)
{
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if(fd < 0)
    {
        return false;
    }

    struct stat st;
    if(fstat(fd, &st) != 0)
    {
        close(fd);
        return false;
    }

    size_t fileSize = static_cast<size_t>(st.st_size);
    size_t frames   = fileSize / frameBytes_;

    std::stringstream ss;
    if(frames < 1)
    {
        ss << path << " is smaller than one " << frameWidth_ << "x" << frameHeight_
           << " RGB565 frame (" << frameBytes_ << " bytes); check frameWidth/frameHeight";
        Logger::write(Logger::ZONE_ERROR, "AnimatedImage", ss.str());
        close(fd);
        return false;
    }
    if(fileSize % frameBytes_ != 0)
    {
        ss << path << " is not a whole number of " << frameWidth_ << "x" << frameHeight_
           << " RGB565 frames; ignoring the last " << (fileSize % frameBytes_)
           << " bytes (check frameWidth/frameHeight)";
        Logger::write(Logger::ZONE_WARNING, "AnimatedImage", ss.str());
        ss.str("");
    }

#ifdef POSIX_FADV_WILLNEED
    // Start reading the frames in the background so the first loop does not wait on the SD card
    posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
#endif
    close(fd);

    path_            = path;
    fileBytes_       = frames * frameBytes_;
    frameCount_      = static_cast<int>(frames);
    frameFileBytes_ += fileBytes_;

    ss << "Using " << frameCount_ << " frames of " << frameWidth_ << "x" << frameHeight_
       << " (" << (fileBytes_ / 1024) << " KB) from " << path;
    Logger::write(Logger::ZONE_INFO, "AnimatedImage", ss.str());

    return true;
}


// Reads a frame into frame_. The file is open only for this read, so it
// never keeps its drive busy. On failure frame_ keeps the previous frame.
bool AnimatedImage::loadFrame(int frame)
{
    std::stringstream ss;

    int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if(fd < 0)
    {
        if(!warnedRead_)
        {
            warnedRead_ = true;
            ss << "Cannot open " << path_ << " (" << strerror(errno) << "); keeping the current frame";
            Logger::write(Logger::ZONE_WARNING, "AnimatedImage", ss.str());
        }
        return false;
    }

    // The file may have been replaced (e.g. from a computer, over USB)
    struct stat st;
    if(fstat(fd, &st) == 0)
    {
        size_t frames = static_cast<size_t>(st.st_size) / frameBytes_;
        size_t bytes  = frames * frameBytes_;
        if(bytes != fileBytes_ && frames >= 1)
        {
            frameFileBytes_ = frameFileBytes_ - fileBytes_ + bytes;
            fileBytes_      = bytes;
            frameCount_     = static_cast<int>(frames);
        }
    }

    size_t done = 0;
    int    err  = 0;
    if(frame >= 0 && frame < frameCount_)
    {
        off_t offset = static_cast<off_t>(frame) * static_cast<off_t>(frameBytes_);
        while(done < frameBytes_)
        {
            ssize_t n = pread(fd, frame_ + done, frameBytes_ - done, offset + static_cast<off_t>(done));
            if(n < 0 && errno == EINTR)
            {
                continue;
            }
            if(n <= 0)
            {
                err = (n < 0) ? errno : 0;
                break;
            }
            done += static_cast<size_t>(n);
        }
    }
    close(fd);

    if(done < frameBytes_)
    {
        if(!warnedRead_)
        {
            warnedRead_ = true;
            ss << "Cannot read frame " << frame << " of " << path_
               << (err ? std::string(" (") + strerror(err) + ")" : std::string(" (file too short)"))
               << "; keeping the current frame";
            Logger::write(Logger::ZONE_WARNING, "AnimatedImage", ss.str());
        }
        return false;
    }

    if(warnedRead_)
    {
        warnedRead_ = false;
        Logger::write(Logger::ZONE_INFO, "AnimatedImage", "Reading " + path_ + " again");
    }
    loadedFrame_ = frame;
    return true;
}


// Drops the file from the page cache, so whatever runs next (usually a game)
// gets that memory back
void AnimatedImage::dropFileCache()
{
#ifdef POSIX_FADV_DONTNEED
    int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if(fd >= 0)
    {
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        close(fd);
    }
#endif
}


void AnimatedImage::allocateGraphicsMemory()
{
    Component::allocateGraphicsMemory();

    if(surface_)
    {
        return;
    }

    if(frameWidth_ <= 0 || frameHeight_ <= 0)
    {
        std::stringstream ss;
        ss << "Invalid frame size " << frameWidth_ << "x" << frameHeight_ << " for " << file_;
        Logger::write(Logger::ZONE_ERROR, "AnimatedImage", ss.str());
        return;
    }
    frameBytes_ = static_cast<size_t>(frameWidth_) * static_cast<size_t>(frameHeight_) * 2;

    SDL_LockMutex(SDL::getMutex());

    bool found = openFile(file_);
    if(!found && altFile_ != "" && altFile_ != file_)
    {
        found = openFile(altFile_);
    }

    if(found)
    {
        // Resume where we left off (e.g. after a game), unless the file changed under us
        if(currentFrame_ >= frameCount_)
        {
            phase_        = 0;
            currentFrame_ = 0;
        }

        loadedFrame_ = -1;
        frame_       = static_cast<Uint8 *>(malloc(frameBytes_));
        if(!frame_)
        {
            Logger::write(Logger::ZONE_ERROR, "AnimatedImage", "Out of memory for a frame of " + path_);
        }
        else if(loadFrame(currentFrame_))
        {
            surface_ = SDL_CreateRGBSurfaceFrom(frame_, frameWidth_, frameHeight_, 16, frameWidth_ * 2,
                                                0xF800, 0x07E0, 0x001F, 0);
            if(!surface_)
            {
                Logger::write(Logger::ZONE_ERROR, "AnimatedImage", "Could not create frame surface: " + std::string(SDL_GetError()));
            }
            else if(useColorKey_)
            {
                // No SDL_RLEACCEL: RLE would cache the first frame and the animation would stop
                SDL_SetColorKey(surface_, SDL_SRCCOLORKEY, colorKey_);
            }
        }

        if(!surface_)
        {
            free(frame_);
            frame_           = NULL;
            loadedFrame_     = -1;
            frameFileBytes_ -= fileBytes_;
            fileBytes_       = 0;
            path_.clear();
        }
    }
    else
    {
        Logger::write(Logger::ZONE_ERROR, "AnimatedImage", "Could not open " + file_ +
                      (altFile_ != "" ? " or " + altFile_ : std::string("")));
    }

    SDL_UnlockMutex(SDL::getMutex());

    if(surface_)
    {
        baseViewInfo.ImageWidth  = frameWidth_ * scaleX_;
        baseViewInfo.ImageHeight = frameHeight_ * scaleY_;
        drawnFrame_ = -1;
    }
}


void AnimatedImage::freeGraphicsMemory()
{
    Component::freeGraphicsMemory();

    SDL_LockMutex(SDL::getMutex());
    if(surface_)
    {
        SDL_FreeSurface(surface_);
        surface_ = NULL;
    }
    SDL_UnlockMutex(SDL::getMutex());

    if(frame_)
    {
        free(frame_);
        frame_ = NULL;
    }
    loadedFrame_ = -1;

    if(!path_.empty())
    {
        dropFileCache();
        frameFileBytes_ -= fileBytes_;
        fileBytes_       = 0;
        Logger::write(Logger::ZONE_INFO, "AnimatedImage", "Released " + path_);
        path_.clear();
    }

    drawnFrame_ = -1;
}


float AnimatedImage::currentFps() const
{
    if(!enabled_)
    {
        return 0;
    }

    float fps = fps_;
    if(idleTimeout_ > 0 &&
       SDL_GetTicks() - lastActivityTicks_ > static_cast<Uint32>(idleTimeout_ * 1000))
    {
        fps = idleFps_;
    }

    if(fps > maxFps_)
    {
        fps = maxFps_;
    }

    return (fps > 0) ? fps : 0;
}


void AnimatedImage::update(float dt)
{
    Component::update(dt);

    if(!surface_ || frameCount_ < 2 || dt <= 0)
    {
        return;
    }

    float fps = currentFps();
    if(fps <= 0)
    {
        return;
    }

    phase_ += static_cast<double>(dt) * fps;
    if(loop_)
    {
        if(phase_ >= frameCount_)
        {
            phase_ = std::fmod(phase_, static_cast<double>(frameCount_));
        }
    }
    else if(phase_ > frameCount_ - 1)
    {
        phase_ = frameCount_ - 1;
    }

    currentFrame_ = static_cast<int>(phase_);
    if(currentFrame_ >= frameCount_)
    {
        currentFrame_ = frameCount_ - 1;
    }
}


bool AnimatedImage::mustRender()
{
    // Only ask for a redraw when a new frame is due and the image can be seen
    return surface_ && currentFrame_ != drawnFrame_ && baseViewInfo.Alpha > 0;
}


void AnimatedImage::draw()
{
    Component::draw();

    if(!surface_)
    {
        return;
    }

    drawnFrame_ = currentFrame_;
    if(baseViewInfo.Alpha <= 0)
    {
        return;
    }

    // If the frame can't be read (the drive is shared with a computer), the previous one stays
    if(currentFrame_ != loadedFrame_)
    {
        loadFrame(currentFrame_);
    }

    SDL_Rect rect;
    rect.x = static_cast<int>(baseViewInfo.XRelativeToOrigin());
    rect.y = static_cast<int>(baseViewInfo.YRelativeToOrigin());
    rect.w = static_cast<int>(baseViewInfo.ScaledWidth());
    rect.h = static_cast<int>(baseViewInfo.ScaledHeight());

    if(!warnedScaling_ && (rect.w != frameWidth_ || rect.h != frameHeight_))
    {
        warnedScaling_ = true;
        std::stringstream ss;
        ss << file_ << " is drawn at " << rect.w << "x" << rect.h << " but its frames are "
           << frameWidth_ << "x" << frameHeight_
           << "; every frame gets rescaled, which is slow. Render the frames at the size they are shown.";
        Logger::write(Logger::ZONE_WARNING, "AnimatedImage", ss.str());
    }

    SDL::renderCopy(surface_, baseViewInfo.Alpha, NULL, &rect, baseViewInfo);
}
