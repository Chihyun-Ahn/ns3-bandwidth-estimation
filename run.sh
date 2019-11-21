#!/bin/bash

../waf --run HAS 2>&1 | tee result.txt
awk -f parse-dash.awk result.txt
#gnuplot dash-ploter