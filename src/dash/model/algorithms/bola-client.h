/*
 * bola-client.h
 *
 * BOLA (Buffer Occupancy based Lyapunov Algorithm) ABR controller.
 * Spiteri, Urgaonkar, Sitaraman, "BOLA: Near-Optimal Bitrate Adaptation for Online Videos",
 * IEEE INFOCOM 2016 / IEEE/ACM ToN 2020 — as implemented in the dash.js reference player (BolaRule).
 */

#ifndef BOLA_CLIENT_H_
#define BOLA_CLIENT_H_

#include <ns3/dash-client.h>

namespace ns3
{

class BolaClient : public DashClient
{
    friend class MpegPlayer;
    friend class FrameBuffer;

  public:
    static TypeId GetTypeId(void);

    BolaClient();

    virtual ~BolaClient();

    virtual void CalcNextSegment(uint32_t currRate, uint32_t& nextRate, Time& delay);
};

} /* namespace ns3 */

#endif /* BOLA_CLIENT_H_ */
