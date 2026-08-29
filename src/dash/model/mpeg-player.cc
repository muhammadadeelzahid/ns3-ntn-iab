/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 TEI of Western Macedonia, Greece
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Dimitrios J. Vergados <djvergad@gmail.com>
 */

#include <iostream>
#include "mpeg-player.h"

#include "dash-client.h"
#include "http-header.h"
#include "mpeg-header.h"

#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE("MpegPlayer");

namespace ns3
{

FrameBuffer::FrameBuffer(uint32_t& capacity)
    : m_capacity(capacity)
{
}

bool
FrameBuffer::push(Ptr<Packet> frame)
{
    NS_LOG_FUNCTION(this);
    if (m_size_in_bytes + frame->GetSerializedSize() <= m_capacity)
    {
        m_queue.push(frame);
        NS_LOG_INFO("pushing packet with size = " << frame->GetSerializedSize());

        m_size_in_bytes += frame->GetSerializedSize();
        return true;
    }
    else
    {
        return false;
    }
}

Ptr<Packet>
FrameBuffer::pop()
{
    NS_LOG_FUNCTION(this);
    Ptr<Packet> frame = m_queue.front();
    m_queue.pop();
    NS_LOG_INFO("popping packet with size = " << frame->GetSerializedSize());

    m_size_in_bytes -= frame->GetSerializedSize();
    return frame;
}

int
FrameBuffer::size()
{
    NS_LOG_FUNCTION(this);
    return m_queue.size();
}

bool
FrameBuffer::empty()
{
    NS_LOG_FUNCTION(this);
    return m_queue.empty();
}

MpegPlayer::MpegPlayer(Ptr<DashClient> dashClient, uint32_t& capacity)
    : m_state(MPEG_PLAYER_NOT_STARTED),
      m_interruption_time(Seconds(0)),
      m_interrruptions(0),
      m_totalRate(0),
      m_minRate(100000000),
      m_framesPlayed(0),
      m_totalPlaybackTime(Seconds(0)),
      m_frameBuffer(capacity),
      m_lastpaused(Seconds(0)),
      m_bufferDelay("0s"),
      m_dashClient(dashClient)
{
    NS_LOG_FUNCTION(this);
}

MpegPlayer::~MpegPlayer()
{
    NS_LOG_FUNCTION(this);
}

int
MpegPlayer::GetQueueSize()
{
    return m_frameBuffer.size();
}

Time
MpegPlayer::GetRealPlayTime(Time playTime)
{
    NS_LOG_INFO(" Start: " << m_start_time.GetSeconds()
                           << " Inter: " << m_interruption_time.GetSeconds()
                           << " playtime: " << playTime.GetSeconds()
                           << " now: " << Simulator::Now().GetSeconds() << " actual: "
                           << (m_start_time + m_interruption_time + playTime).GetSeconds());

    return m_start_time + m_interruption_time +
           (m_state == MPEG_PLAYER_PAUSED ? (Simulator::Now() - m_lastpaused) : Seconds(0)) +
           playTime - Simulator::Now();
}

bool
MpegPlayer::ReceiveFrame(Ptr<Packet> message)
{
    NS_LOG_FUNCTION(this << message);
    NS_LOG_INFO("Received Frame " << m_state);

    Ptr<Packet> msg = message->Copy();

    if (!m_frameBuffer.push(msg))
    {
        return false;
    }

    if (m_state == MPEG_PLAYER_PAUSED)
    {
        NS_LOG_UNCOND("MpegPlayer Play resumed at t=" << Simulator::Now().GetSeconds() 
                      << "s. Player ID=" << (m_dashClient ? m_dashClient->m_id : -1)
                      << " Pause duration=" << (Simulator::Now() - m_lastpaused).GetSeconds() 
                      << "s. Total interruption time=" << m_interruption_time.GetSeconds() << "s");
        m_state = MPEG_PLAYER_PLAYING;
        m_interruption_time += (Simulator::Now() - m_lastpaused);
        // Resume through the single guarded PlayFrame event (cancel any pending underrun keep-alive tick
        // first) so resuming does NOT spawn a second concurrent PlayFrame chain.
        m_playFrameEvent.Cancel();
        m_playFrameEvent = Simulator::Schedule(MilliSeconds(0), &MpegPlayer::PlayFrame, this);
    }
    else if (m_state == MPEG_PLAYER_NOT_STARTED)
    {
        NS_LOG_INFO("Play started");
        m_state = MPEG_INITIAL_BUFFERING;
        m_playFrameEvent.Cancel();
        m_playFrameEvent = Simulator::Schedule(Seconds(1), &MpegPlayer::PlayFrame, this);
    }
    return true;
}

void
MpegPlayer::Start(void)
{
    NS_LOG_FUNCTION(this);
    m_state = MPEG_PLAYER_PLAYING;
    m_interruption_time = Seconds(0);
}

void
MpegPlayer::PlayFrame(void)
{
    NS_LOG_FUNCTION(this);
    if (m_state == MPEG_PLAYER_DONE)
    {
        return;
    }
    else if (m_state == MPEG_INITIAL_BUFFERING)
    {
        m_start_time = Simulator::Now();
        m_state = MPEG_PLAYER_PLAYING;
    }
    if (m_frameBuffer.empty())
    {
        NS_LOG_UNCOND("MpegPlayer No frames to play at t=" << Simulator::Now().GetSeconds() 
                      << "s. Player ID=" << (m_dashClient ? m_dashClient->m_id : -1)
                      << " State changing to PAUSED. Interruptions=" << m_interrruptions);
        // Count/timestamp the interruption ONLY on the transition into a stall, not on every 100 ms
        // PlayFrame tick while already paused. Otherwise the reschedule below would (a) inflate the
        // interruption count and (b) keep resetting m_lastpaused so the resume in ReceiveFrame
        // (m_interruption_time += Now - m_lastpaused) would undercount the true stall duration.
        if (m_state != MPEG_PLAYER_PAUSED)
        {
            m_state = MPEG_PLAYER_PAUSED;
            m_lastpaused = Simulator::Now();
            m_interrruptions++;
        }

        // Proactively request next segment if client is available
        if (m_dashClient && !m_dashClient->m_RequestPending)
        {
            NS_LOG_INFO("Proactively requesting segment due to buffer underrun");
            Simulator::Schedule(MilliSeconds(0), &DashClient::RequestSegment, m_dashClient);
        }
        // Keep the player alive during an underrun by rescheduling. If the buffer empties while a
        // segment request is still pending (RequestPending==true, common when QUIC stalls a segment),
        // failing to reschedule here would let PlayFrame die: when that pending segment later fails and
        // the watchdog frees the client (RequestPending=false), the underrun re-request above lives
        // inside PlayFrame and could never fire again, wedging the client forever. Rescheduling lets the
        // client re-request and recover on the next tick.
        m_playFrameEvent.Cancel();
        m_playFrameEvent = Simulator::Schedule(MilliSeconds(100), &MpegPlayer::PlayFrame, this);
        return;
    }

    Ptr<Packet> message = m_frameBuffer.pop();

    MPEGHeader mpeg_header;
    HTTPHeader http_header;
    const uint32_t headerSize = http_header.GetSerializedSize() + mpeg_header.GetSerializedSize();

    if (message->GetSize() < headerSize)
    {
        NS_LOG_UNCOND("Dropping undersized frame: size=" << message->GetSize()
                                                       << " headerSize=" << headerSize);
        m_playFrameEvent.Cancel();
        m_playFrameEvent = Simulator::Schedule(MilliSeconds(100), &MpegPlayer::PlayFrame, this);
        return;
    }

    uint32_t removedHttp = message->RemoveHeader(http_header);
    uint32_t removedMpeg = message->RemoveHeader(mpeg_header);
    if (removedHttp != http_header.GetSerializedSize() ||
        removedMpeg != mpeg_header.GetSerializedSize())
    {
        NS_LOG_DEBUG("Dropping corrupted frame in player: removedHttp=" << removedHttp
                    << " removedMpeg=" << removedMpeg
                    << " size=" << message->GetSize());
        m_playFrameEvent.Cancel();
        m_playFrameEvent = Simulator::Schedule(MilliSeconds(100), &MpegPlayer::PlayFrame, this);
        return;
    }

    m_totalRate += http_header.GetResolution();
    if (http_header.GetSegmentId() > 0) // Discard the first segment for the minRate
    {                                   // calculation, as it is always the minimum rate
        m_minRate =
            http_header.GetResolution() < m_minRate ? http_header.GetResolution() : m_minRate;
    }
    m_framesPlayed++;
    // Track total playback time: each frame adds MPEG_TIME_BETWEEN_FRAMES milliseconds
    m_totalPlaybackTime += MilliSeconds(MPEG_TIME_BETWEEN_FRAMES);

    NS_LOG_UNCOND(this << " " << Simulator::Now().GetSeconds()
                << " PLAYING FRAME: "
                << " PlayerId: " << m_dashClient->m_id << " VidId: " << http_header.GetVideoId()
                << " SegId: " << http_header.GetSegmentId() << " Res: "
                << http_header.GetResolution() << " FrameId: " << mpeg_header.GetFrameId()
                << " PlayTime: " << mpeg_header.GetPlaybackTime().GetSeconds()
                << " Type: " << (char)mpeg_header.GetType() << " Size: " << mpeg_header.GetSize()
                << " interTime: " << m_interruption_time.GetSeconds()
                << " queueLength: " << m_frameBuffer.size());

    m_playFrameEvent.Cancel();
    m_playFrameEvent = Simulator::Schedule(MilliSeconds(MPEG_TIME_BETWEEN_FRAMES), &MpegPlayer::PlayFrame, this);

    // There may be space now to read a new packet from the socket
    // Trigger CheckBuffer immediately after consuming a frame to ensure continuous reception
    if (m_dashClient)
    {
        Simulator::Schedule(MilliSeconds(0), &DashClient::CheckBuffer, m_dashClient);
    }
}

void
MpegPlayer::FinalizeInterruptionTime()
{
    // If player was paused (or marked as DONE while paused), calculate the final interruption time
    // This handles the case where the simulation ends while the player is paused
    // Note: State might be MPEG_PLAYER_DONE if StopApplication() was called, but we still need
    // to account for the interruption time if the player was paused when it was marked as DONE
    NS_LOG_UNCOND("MpegPlayer FinalizeInterruptionTime called. State: " << m_state 
                  << " (MPEG_PLAYER_PAUSED=" << MPEG_PLAYER_PAUSED 
                  << ", MPEG_PLAYER_DONE=" << MPEG_PLAYER_DONE 
                  << "), interruptions: " << m_interrruptions 
                  << ", current interruption_time: " << m_interruption_time.GetSeconds() << "s");
    
    // Check if player was paused (or done) and has interruptions that weren't accounted for
    // We check for DONE state because StopApplication() may have set it before GetStats() is called
    if (m_interrruptions > 0 && (m_state == MPEG_PLAYER_PAUSED || m_state == MPEG_PLAYER_DONE))
    {
        // Calculate time from last pause to now
        // This accounts for the interruption that occurred but wasn't recorded because playback never resumed
        // If state is DONE, we use the time when it was marked as DONE (which should be close to Now())
        // but we still want to account for the time from when it was paused
        Time finalInterruptionTime = Simulator::Now() - m_lastpaused;
        NS_LOG_UNCOND("MpegPlayer Player was paused/done. m_lastpaused: " << m_lastpaused.GetSeconds() 
                      << "s, Now: " << Simulator::Now().GetSeconds() 
                      << "s, finalInterruptionTime: " << finalInterruptionTime.GetSeconds() << "s");
        m_interruption_time += finalInterruptionTime;
        NS_LOG_UNCOND("MpegPlayer Updated interruption_time: " << m_interruption_time.GetSeconds() << "s");
    }
    else
    {
        NS_LOG_UNCOND("MpegPlayer Condition not met. State: " << m_state 
                      << ", interruptions: " << m_interrruptions 
                      << " (expected state to be PAUSED=" << MPEG_PLAYER_PAUSED 
                      << " or DONE=" << MPEG_PLAYER_DONE << " and interruptions > 0)");
    }
}

} // namespace ns3
