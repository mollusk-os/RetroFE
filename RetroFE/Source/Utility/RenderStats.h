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

#include <stdint.h>
#include <string>

// Optional render and memory statistics, enabled with "renderStatsFile = <path>"
// in settings.conf. Every few seconds one line is appended to that file with
// the number of redraws, how long drawing and flipping took, RetroFE's CPU use
// and its resident memory. Meant for measuring animated backgrounds on hardware.
class RenderStats
{
public:
    RenderStats();

    void setFile(const std::string &path);   // empty path = disabled
    bool enabled() const { return !file_.empty(); }

    static uint64_t nowUs();

    // Record one redraw: time spent drawing the page and time spent flipping it to the screen
    void addRender(uint64_t drawUs, uint64_t flipUs);

    // Call once per main loop iteration; writes a line when the interval has elapsed
    void tick();

    // Start a fresh interval, e.g. after RetroFE was blocked while a game ran
    void reset();

private:
    void write(double seconds);

    std::string file_;
    uint64_t    intervalStartUs_;
    uint64_t    cpuStartUs_;
    unsigned    renders_;
    uint64_t    drawTotalUs_;
    uint64_t    drawMaxUs_;
    uint64_t    flipTotalUs_;
};
