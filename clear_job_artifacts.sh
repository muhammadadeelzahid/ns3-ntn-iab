#!/bin/bash
rm *.txt *.err *.out *.log -rf Quic_artifacts Tcp_artifacts
python3 ./waf build