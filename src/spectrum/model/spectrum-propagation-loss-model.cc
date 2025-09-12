/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 CTTC
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
 * Author: Nicola Baldo <nbaldo@cttc.es>
 */


#include "spectrum-propagation-loss-model.h"
#include <ns3/log.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SpectrumPropagationLossModel");

NS_OBJECT_ENSURE_REGISTERED (SpectrumPropagationLossModel);

SpectrumPropagationLossModel::SpectrumPropagationLossModel ()
  : m_next (0)
{
  this->m_useSatelliteModel = false;
}

SpectrumPropagationLossModel::~SpectrumPropagationLossModel ()
{
}

void
SpectrumPropagationLossModel::DoDispose ()
{
  m_next = 0;
}

TypeId
SpectrumPropagationLossModel::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SpectrumPropagationLossModel")
    .SetParent<Object> ()
    .SetGroupName ("Spectrum")
  ;
  return tid;
}

void SpectrumPropagationLossModel::SetUseSatelliteModel(bool flag)
{
  m_useSatelliteModel = flag;
}

bool SpectrumPropagationLossModel::GetUseSatelliteModel() const
{
  return m_useSatelliteModel;
}

void SpectrumPropagationLossModel::SetNext (Ptr<SpectrumPropagationLossModel> next)
{
  m_next = next;
}


Ptr<SpectrumValue>
SpectrumPropagationLossModel::CalcRxPowerSpectralDensity (Ptr<const SpectrumValue> txPsd,
                                                          Ptr<const MobilityModel> a,
                                                          Ptr<const MobilityModel> b) const
{
  Ptr<SpectrumValue> rxPsd = DoCalcRxPowerSpectralDensity (txPsd, a, b);
  if (m_next != 0)
    {
      rxPsd = m_next->CalcRxPowerSpectralDensity (rxPsd, a, b);
    }
  return rxPsd;
}

Ptr<SpectrumValue>
SpectrumPropagationLossModel::CalcRxPowerSpectralDensityMultiLayers (Ptr<const SpectrumValue> txPsd,
                                                                    Ptr<const MobilityModel> a,
                                                                    Ptr<const MobilityModel> b,
                                                                    uint8_t txLayerInd,
                                                                    uint8_t rxLayerInd) const
{
  Ptr<SpectrumValue> rxPsd = DoCalcRxPowerSpectralDensityMultilayers (txPsd, a, b,txLayerInd,rxLayerInd);
  if (m_next != 0)
    {
      rxPsd = m_next->CalcRxPowerSpectralDensityMultiLayers (rxPsd, a, b,txLayerInd,rxLayerInd);
    }
  return rxPsd;
}

Ptr<SpectrumValue>
SpectrumPropagationLossModel::DoCalcRxPowerSpectralDensityMultilayers (
    Ptr<const SpectrumValue> txPsd,
    Ptr<const MobilityModel> a,
    Ptr<const MobilityModel> b,
    uint8_t txLayerInd,
    uint8_t rxLayerInd) const
{
  if (m_next)
    {
      return m_next->CalcRxPowerSpectralDensityMultiLayers (txPsd, a, b, txLayerInd, rxLayerInd);
    }
  else
    {
      NS_FATAL_ERROR ("DoCalcRxPowerSpectralDensityMultilayers() not implemented in subclass");
    }
}

} // namespace ns3
