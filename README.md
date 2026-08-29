# Adaptive Video Streaming for Satellite backhauled 5G IAB Network

This repository is an **ns-3 codebase** that combines **Non-Terrestrial Networks (NTN)** and **Integrated Access and Backhaul (IAB)** on top of the ns-3 **mmWave** module, and includes end-to-end application experiments with **MPEG-DASH video streaming** over **TCP** and **QUIC**.

The IAB node is backhauled by a **LEO-satellite feeder link** and can perform an **inter-satellite backhaul handover** as the serving satellite changes. On top of this, the DASH client supports a suite of **adaptive-bitrate (ABR) algorithms** (including BOLA), so video Quality of Experience can be evaluated against transport, congestion control, buffering, and handover conditions.

For detailed methodology, analysis, and results related to this project, please refer to our article on [arXiv](https://arxiv.org/abs/2604.16634).

Developed at the **University of Manitoba**. 

## Modules and upstream code integrated

This codebase integrates and extends the following upstream modules:

- **Integrated Access and Backhaul (IAB)**: from the [ns3-mmwave](https://github.com/nyuwireless-unipd/ns3-mmwave)
- **Non-Terrestrial Networks (NTN)**: from [ns-3-ntn](https://gitlab.com/mattiasandri/ns-3-ntn)
- **Multilayer base stations and Hybrid Beamforming (HBF)**: from [ns3-mmwave-hbf](https://github.com/signetlabdei/ns3-mmwave-hbf)
- **Traffic generators (NR/5G-LENA)**: from [5G-LENA](https://cttc-lena.gitlab.io/nr/html/)
- **QUIC**: from [quic-ns-3](https://github.com/signetlabdei/quic-ns-3)
- **MPEG-DASH (HTTP Adaptive Streaming)**: from [djvergad/dash](https://github.com/djvergad/dash)

## Build

This repository uses the **waf** build system (the classic ns-3 workflow).

Configure and build:

```bash
./waf configure --enable-examples
./waf build
```

## Run

Run any of the `scratch/` programs using `--run` (examples below):

```bash
./waf --run "ntn-iab"
```

MPEG-DASH video streaming experiments:

```bash
./waf --run "ntn-iab-tcp-dash"
./waf --run "ntn-iab-quic-dash"
```

You can pass program arguments after `--run`, for example:

```bash
./waf --run "ntn-iab-quic-dash --numUes=10 --numRelay=1"
```

### Key parameters (DASH scenarios)

The `ntn-iab-tcp-dash` and `ntn-iab-quic-dash` programs accept the following arguments:

| Argument         | Description                                                                 |
|------------------|-----------------------------------------------------------------------------|
| `ccAlgorithm`    | Congestion control (TCP: `ns3::TcpNewReno`, `ns3::TcpBbr`; QUIC: `ns3::QuicCongestionControl`, `ns3::QuicBbr`) |
| `abrAlgorithm`   | ABR client (e.g. `ns3::BolaClient`, `ns3::RaahsClient`, `ns3::FdashClient`)  |
| `numUes`         | Number of UEs served by the IAB cell                                        |
| `numRelay`       | Number of IAB relay nodes                                                    |
| `numSat`         | Number of donor satellites (`>= 2` enables backhaul handover)               |
| `hoTime`         | Inter-handover interval in seconds (`0` disables handover)                  |
| `backhaulRate`   | LEO feeder-link capacity (e.g. `100Mbps`)                                    |
| `feederDelay`    | Feeder-link one-way propagation delay in seconds                            |
| `hoExecDelay`    | Modeled handover interruption (downlink pause) in seconds                    |
| `targetDt`       | DASH target playback buffer in seconds                                       |
| `maxBufferS`     | Hard playback-buffer cap in seconds (`0` = unlimited)                        |
| `simDuration`    | Video/simulation duration in seconds                                        |

Example: a single-UE LEO session with four backhaul handovers, BOLA ABR, and TCP NewReno:

```bash
./waf --run "ntn-iab-tcp-dash --ccAlgorithm=ns3::TcpNewReno --abrAlgorithm=ns3::BolaClient \
  --numUes=1 --numRelay=1 --numSat=5 --hoTime=150 --backhaulRate=100Mbps --simDuration=600"
```

## Maintainers

- **Muhammad Adeel Zahid** (`adeel.m.zahid@gmail.com`)
- **WiCoNS Research Group** (University of Manitoba): [website](https://home.cc.umanitoba.ca/~hossaina/wicons/index.html)
- **Advancing LEO Satellite Networks and Systems Group** (University of Manitoba): [website](https://home.cc.umanitoba.ca/~hup2/ntn/)

## License

This software is licensed under the terms of the **GNU GPLv2**, consistent with ns-3. See the [`LICENSE`](LICENSE) file for details.
