/*
 * bola-client.cc
 *
 * BOLA (Buffer Occupancy based Lyapunov Algorithm) ABR controller, ported faithfully from the
 * dash.js reference player's BolaRule (Dash-Industry-Forum/dash.js,
 * src/streaming/rules/abr/BolaRule.js), itself based on Spiteri et al., INFOCOM 2016.
 *
 * Decision: pick the representation maximizing  s_i = (Vp*(utility_i - 1 + gp) - bufferLevel)/bitrate_i,
 * with  utility_i = ln(bitrate_i/bitrate_0) + 1,  gp = (utility_max - 1)/(bufferTime/MIN_BUFFER - 1),
 * Vp = MIN_BUFFER/gp,  bufferTime = max(targetBuffer, MIN_BUFFER + MIN_BUFFER_PER_LEVEL*N).
 * It is buffer-based and needs no bandwidth estimate.
 */

#include "bola-client.h"

#include "../mpeg-header.h"

#include <ns3/dash-client.h>
#include <ns3/log.h>
#include <ns3/simulator.h>

#include <algorithm>
#include <cmath>
#include <vector>

// dash.js BolaRule constants (seconds)
#define BOLA_MINIMUM_BUFFER_S 10.0
#define BOLA_MINIMUM_BUFFER_PER_BITRATE_LEVEL_S 2.0

NS_LOG_COMPONENT_DEFINE("BolaClient");

namespace ns3
{
NS_OBJECT_ENSURE_REGISTERED(BolaClient);

TypeId
BolaClient::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::BolaClient").SetParent<DashClient>().AddConstructor<BolaClient>();
    return tid;
}

BolaClient::BolaClient()
{
}

BolaClient::~BolaClient()
{
}

void
BolaClient::CalcNextSegment(uint32_t currRate, uint32_t& nextRate, Time& delay)
{
    const uint32_t N = rates.size();
    delay = Seconds(0);

    if (N < 2)
    {
        nextRate = rates.front();
        return;
    }

    // Segment duration p (seconds): MPEG_FRAMES_PER_SEGMENT frames * MPEG_TIME_BETWEEN_FRAMES ms.
    const double p = (MPEG_FRAMES_PER_SEGMENT * MPEG_TIME_BETWEEN_FRAMES) / 1000.0;

    // Utilities normalized so utilities[0] == 1 (rates[] is ascending: rates[0] is the lowest).
    std::vector<double> utilities(N);
    for (uint32_t i = 0; i < N; ++i)
    {
        utilities[i] = std::log((double)rates[i]) - std::log((double)rates[0]) + 1.0;
    }

    // BOLA parameters, chosen so the lowest rate is preferred at minimum buffer and the highest at
    // bufferTime (dash.js BolaRule).
    const double bufferTarget = m_target_dt.GetSeconds();
    const double bufferTime =
        std::max(bufferTarget, BOLA_MINIMUM_BUFFER_S + BOLA_MINIMUM_BUFFER_PER_BITRATE_LEVEL_S * N);
    const double gp = (utilities[N - 1] - 1.0) / (bufferTime / BOLA_MINIMUM_BUFFER_S - 1.0);
    const double Vp = BOLA_MINIMUM_BUFFER_S / gp;

    const double bufferLevel = GetBufferEstimate(); // current playout buffer in seconds

    // Select the representation with the maximum BOLA score; ties favour the higher bitrate.
    uint32_t quality = 0;
    double score = 0.0;
    bool first = true;
    for (uint32_t i = 0; i < N; ++i)
    {
        double s = (Vp * (utilities[i] - 1.0 + gp) - bufferLevel) / (double)rates[i];
        if (first || s >= score)
        {
            score = s;
            quality = i;
            first = false;
        }
    }
    nextRate = rates[quality];

    // BOLA pacing: bound the buffer near bufferTime so the buffer-based decision stays meaningful
    // (without it the buffer saturates on a fast link and BOLA always selects the maximum bitrate).
    if (bufferLevel + p > bufferTime)
    {
        delay = Seconds(bufferLevel + p - bufferTime);
    }

    NS_LOG_INFO("BOLA bufferLevel=" << bufferLevel << " Vp=" << Vp << " gp=" << gp << " quality="
                                    << quality << " nextRate=" << nextRate << " currRate=" << currRate
                                    << " delay=" << delay.GetSeconds());
}

} /* namespace ns3 */
