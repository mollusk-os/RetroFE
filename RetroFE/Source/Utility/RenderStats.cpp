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
#include "RenderStats.h"
#include "../Graphics/Component/AnimatedImage.h"
#include <cstdio>
#include <cstring>
#include <time.h>
#include <sys/resource.h>

#define RENDER_STATS_INTERVAL_US 5000000ULL


RenderStats::RenderStats()
    : intervalStartUs_(0)
    , cpuStartUs_(0)
    , renders_(0)
    , drawTotalUs_(0)
    , drawMaxUs_(0)
    , flipTotalUs_(0)
{
}


uint64_t RenderStats::nowUs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000;
}


static uint64_t cpuTimeUs()
{
    struct rusage usage;
    if(getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000ULL +
           static_cast<uint64_t>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
}


// Read one "<key>: <value> kB" line from /proc/self/status, -1 if missing
static long statusKb(const char *key)
{
    FILE *f = fopen("/proc/self/status", "r");
    if(!f)
    {
        return -1;
    }

    char   line[128];
    long   value  = -1;
    size_t keyLen = strlen(key);
    while(fgets(line, sizeof(line), f))
    {
        if(strncmp(line, key, keyLen) == 0 && line[keyLen] == ':')
        {
            sscanf(line + keyLen + 1, "%ld", &value);
            break;
        }
    }
    fclose(f);

    return value;
}


void RenderStats::setFile(const std::string &path)
{
    file_ = path;
    reset();
}


void RenderStats::reset()
{
    intervalStartUs_ = nowUs();
    cpuStartUs_      = cpuTimeUs();
    renders_         = 0;
    drawTotalUs_     = 0;
    drawMaxUs_       = 0;
    flipTotalUs_     = 0;
}


void RenderStats::addRender(uint64_t drawUs, uint64_t flipUs)
{
    renders_++;
    drawTotalUs_ += drawUs;
    flipTotalUs_ += flipUs;
    if(drawUs > drawMaxUs_)
    {
        drawMaxUs_ = drawUs;
    }
}


void RenderStats::tick()
{
    if(!enabled())
    {
        return;
    }

    uint64_t elapsed = nowUs() - intervalStartUs_;
    if(elapsed < RENDER_STATS_INTERVAL_US)
    {
        return;
    }

    // A much longer interval means the main loop was blocked (a game was running): skip it
    if(elapsed < 2 * RENDER_STATS_INTERVAL_US)
    {
        write(static_cast<double>(elapsed) / 1000000.0);
    }
    reset();
}


void RenderStats::write(double seconds)
{
    FILE *f = fopen(file_.c_str(), "a");
    if(!f)
    {
        return;
    }

    double cpuPercent = 100.0 * static_cast<double>(cpuTimeUs() - cpuStartUs_) / (seconds * 1000000.0);
    double drawAvgMs  = renders_ ? static_cast<double>(drawTotalUs_) / renders_ / 1000.0 : 0.0;
    double flipAvgMs  = renders_ ? static_cast<double>(flipTotalUs_) / renders_ / 1000.0 : 0.0;

    fprintf(f, "t=%.0fs renders=%u (%.1f/s) draw=%.2fms avg %.2fms max flip=%.2fms avg cpu=%.1f%% "
               "rss=%ldkB anon=%ldkB file=%ldkB anim_file=%lukB\n",
            static_cast<double>(nowUs()) / 1000000.0,
            renders_, renders_ / seconds,
            drawAvgMs, static_cast<double>(drawMaxUs_) / 1000.0, flipAvgMs,
            cpuPercent,
            statusKb("VmRSS"), statusKb("RssAnon"), statusKb("RssFile"),
            static_cast<unsigned long>(AnimatedImage::frameFileBytes() / 1024));
    fclose(f);
}
