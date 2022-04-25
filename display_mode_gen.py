#!/usr/bin/env python3
# Converts copy-and-pasted video standards tables into code tables.

import argparse
import dataclasses
import sys
import typing

#
# CEA-861-G
# https://members.cta.tech/ctaPublicationDetails/?id=11016f33-3422-e811-90ce-0003ff528c1a
# https://web.archive.org/web/20171201033424/https://standards.cta.tech/kwspub/published_docs/CTA-861-G_FINAL_revised_2017.pdf
#

# Table 1 Video Format Timings--Detailed Timing Information
CTA_861_TABLE_1 = """
60,65 1280 720 Prog 3300 2020 750 30 18.000 24.0003 59.400
61,66 1280 720 Prog 3960 2680 750 30 18.750 25.000 74.250
62,67 1280 720 Prog 3300 2020 750 30 22.500 30.0003 74.250
108,109 1280 720 Prog 2500 1220 750 30 36.000 48.0003 90.000
32,72 1920 1080 Prog 2750 830 1125 45 27.000 24.0003 74.250
33,73 1920 1080 Prog 2640 720 1125 45 28.125 25.000 74.250
34,74 1920 1080 Prog 2200 280 1125 45 33.750 30.0003 74.250
111,112 1920 1080 Prog 2750 830 1125 45 54.000 48.0003 148.500
79 1680 720 Prog 3300 1620 750 30 18.000 24.0003 59.400
80 1680 720 Prog 3168 1488 750 30 18.750 25.000 59.400
81 1680 720 Prog 2640 960 750 30 22.500 30.0003 59.400
110 1680 720 Prog 2750 1070 750 30 36.000 48.0003 99.000
86 2560 1080 Prog 3750 1190 1100 20 26.400 24.0003 99.000
87 2560 1080 Prog 3200 640 1125 45 28.125 25.000 90.000
88 2560 1080 Prog 3520 960 1125 45 33.750 30.0003 118.800
113 2560 1080 Prog 3750 1190 1100 20 52.800 48.0003 198.000
93,103 3840 2160 Prog 5500 1660 2250 90 54.000 24.0003 297.000
94,104 3840 2160 Prog 5280 1440 2250 90 56.250 25.000 297.000
95,105 3840 2160 Prog 4400 560 2250 90 67.500 30.0003 297.000
114,116 3840 2160 Prog 5500 1660 2250 90 108.000 48.0003 594.000
98 4096 2160 Prog 5500 1404 2250 90 54.000 24.0003 297.000
99 4096 2160 Prog 5280 1184 2250 90 56.250 25.000 297.000
100 4096 2160 Prog 4400 304 2250 90 67.500 30.0003 297.000
115 4096 2160 Prog 5500 1404 2250 90 108.000 48.0003 594.000
121 5120 2160 Prog 7500 2380 2200 40 52.800 24.0003 396.000
122 5120 2160 Prog 7200 2080 2200 40 55.000 25.000 396.000
123 5120 2160 Prog 6000 880 2200 40 66.000 30.0003 396.000
124 5120 2160 Prog 6250 1130 2475 315 118.800 48.0003 742.500
194,202 7680 4320 Prog 11000 3320 4500 180 108.000 24.0003 1188.000
195,203 7680 4320 Prog 10800 3120 4400 80 110.000 25.000 1188.000
196,204 7680 4320 Prog 9000 1320 4400 80 132.000 30.0003 1188.000
197,205 7680 4320 Prog 11000 3320 4500 180 216.000 48.0003 2376.000
210 10240 4320 Prog 12500 2260 4950 630 118.800 24.0003 1485.000
211 10240 4320 Prog 13500 3260 4400 80 110.000 25.000 1485.000
212 10240 4320 Prog 11000 760 4500 180 135.000 30.0003 1485.000
213 10240 4320 Prog 12500 2260 4950 630 237.600 48.0003 2970.000

17,18 720 576 Prog 864 144 625 49 31.250 50.000 27.000
19,68 1280 720 Prog 1980 700 750 30 37.500 50.000 74.250
20 1920 1080 Int 2640 720 1125 22.5 28.125 50.000 74.250
21,22 1440 576 Int 1728 288 625 24.5 15.625 50.000 27.000
23,24 1440 288 Prog 1728 288 312 24 15.625 50.080 27.000
23,24 1440 288 Prog 1728 288 313 25 15.625 49.920 27.000
23,24 1440 288 Prog 1728 288 314 26 15.625 49.761 27.000
25,26 2880 576 Int 3456 576 625 24.5 15.625 50.000 54.000
27,28 2880 288 Prog 3456 576 312 24 15.625 50.080 54.000
27,28 2880 288 Prog 3456 576 313 25 15.625 49.920 54.000
27,28 2880 288 Prog 3456 576 314 26 15.625 49.761 54.000
29,30 1440 576 Prog 1728 288 625 49 31.250 50.000 54.000
31,75 1920 1080 Prog 2640 720 1125 45 56.250 50.000 148.500
37,38 2880 576 Prog 3456 576 625 49 31.250 50.000 108.000
39 1920 1080 Int 2304 384 1250 85 31.250 50.000 72.000
82 1680 720 Prog 2200 520 750 30 37.500 50.000 82.500
89 2560 1080 Prog 3300 740 1125 45 56.250 50.000 185.625
96,106 3840 2160 Prog 5280 1440 2250 90 112.500 50.000 594.000
101 4096 2160 Prog 5280 1184 2250 90 112.500 50.000 594.000
125 5120 2160 Prog 6600 1480 2250 90 112.500 50.000 742.500
198,206 7680 4320 Prog 10800 3120 4400 80 220.000 50.000 2376.000
214 10240 4320 Prog 13500 3260 4400 80 220.000 50.000 2970.000

1 640 480 Prog 800 160 525 45 31.469 59.9403 25.175
2,3 720 480 Prog 858 138 525 45 31.469 59.9403 27.000
4,69 1280 720 Prog 1650 370 750 30 45.000 60.0003 74.250
5 1920 1080 Int 2200 280 1125 22.5 33.750 60.0003 74.250
6,7 1440 480 Int 1716 276 525 22.5 15.734 59.9403 27.000
8,9 1440 240 Prog 1716 276 262 22 15.734 60.0543 27.000
8,9 1440 240 Prog 1716 276 263 23 15.734 59.8263 27.000
10,11 2880 480 Int 3432 552 525 22.5 15.734 59.9403 54.000
12,13 2880 240 Prog 3432 552 262 22 15.734 60.0543 54.000
12,13 2880 240 Prog 3432 552 263 23 15.734 59.8263 54.000
14,15 1440 480 Prog 1716 276 525 45 31.469 59.9403 54.000
16,76 1920 1080 Prog 2200 280 1125 45 67.500 60.0003 148.500
35,36 2880 480 Prog 3432 552 525 45 31.469 59.9403 108.000
83 1680 720 Prog 2200 520 750 30 45.000 60.0003 99.000
90 2560 1080 Prog 3000 440 1100 20 66.000 60.0003 198.000
97,107 3840 2160 Prog 4400 560 2250 90 135.000 60.0003 594.000
102 4096 2160 Prog 4400 304 2250 90 135.000 60.0003 594.000
126 5120 2160 Prog 5500 380 2250 90 135.000 60.0003 742.500
199,207 7680 4320 Prog 9000 1320 4400 80 264.000 60.0003 2376.000
215 10240 4320 Prog 11000 760 4500 180 270.000 60.0003 2970.000

40 1920 1080 Int 2640 720 1125 22.5 56.250 100.00 148.500
41,70 1280 720 Prog 1980 700 750 30 75.000 100.00 148.500
42,43 720 576 Prog 864 144 625 49 62.500 100.00 54.000
44,45 1440 576 Int 1728 288 625 24.5 31.250 100.00 54.000
64,77 1920 1080 Prog 2640 720 1125 45 112.500 100.00 297.000
84 1680 720 Prog 2000 320 825 105 82.500 100.00 165.000
91 2560 1080 Prog 2970 410 1250 170 125.000 100.00 371.250
117,119 3840 2160 Prog 5280 1440 2250 90 225.000 100.00 1188.000
127 5120 2160 Prog 6600 1480 2250 90 225.000 100.00 1485.000
200,208 7680 4320 Prog 10560 2880 4500 180 450.000 100.00 4752.000
216 10240 4320 Prog 13200 2960 4500 180 450.000 100.00 5940.000
218 4096 2160 Prog 5280 1184 2250 90 225.000 100.00 1188.000

46 1920 1080 Int 2200 280 1125 22.5 67.500 120.003 148.500
47,71 1280 720 Prog 1650 370 750 30 90.000 120.003 148.500
48,49 720 480 Prog 858 138 525 45 62.937 119.883 54.000
50,51 1440 480 Int 1716 276 525 22.5 31.469 119.883 54.000
63,78 1920 1080 Prog 2200 280 1125 45 135.000 120.003 297.000
85 1680 720 Prog 2000 320 825 105 99.000 120.003 198.000
92 2560 1080 Prog 3300 740 1250 170 150.000 120.003 495.000
118,120 3840 2160 Prog 4400 560 2250 90 270.000 120.003 1188.000
193 5120 2160 Prog 5500 380 2250 90 270.000 120.003 1485.000
201,209 7680 4320 Prog 8800 1120 4500 180 540.000 120.003 4752.000
217 10240 4320 Prog 11000 760 4500 180 540.000 120.003 5940.000
219 4096 2160 Prog 4400 304 2250 90 270.000 120.003 1188.000

52,53 720 576 Prog 864 144 625 49 125.000 200.00 108.00
54,55 1440 576 Int 1728 288 625 24.5 62.500 200.00 108.00

56,57 720 480 Prog 858 138 525 45 125.874 239.763 108.000
58,59 1440 480 Int 1716 276 525 22.5 62.937 239.763 108.000
"""

# Table 2 Video Format Timings--Detailed Sync Information
CTA_861_TABLE_2 = """
60,65 2 1760 40 220 P 5 5 20 P 1 SMPTE 296M [61] 1,2,25
61,66 2 2420 40 220 P 5 5 20 P 1 SMPTE 296M [61] 1,2
62,67 2 1760 40 220 P 5 5 20 P 1 SMPTE 296M [61] 1,2
108,109 2 960 40 220 P 5 5 20 P 1 SMPTE 296M [61] 1,2,25
32,72 2 638 44 148 P 4 5 36 P 1 SMPTE 274M [2] 14
33,73 2 528 44 148 P 4 5 36 P 1 SMPTE 274M [2] 14
34,74 2 88 44 148 P 4 5 36 P 1 SMPTE 274M [2] 14
111,112 2 638 44 148 P 4 5 36 P 1 SMPTE 274M [2] 14
79 2 1360 40 220 P 5 5 20 P 1 SMPTE 296M [61] 26
80 2 1228 40 220 P 5 5 20 P 1 SMPTE 296M [61] 26
81 2 700 40 220 P 5 5 20 P 1 SMPTE 296M [61] 26
110 2 810 40 220 P 5 5 20 P 1 SMPTE 296M [61] 26
86 2 998 44 148 P 4 5 11 P 1 SMPTE 274M [2] 27
87 2 448 44 148 P 4 5 36 P 1 SMPTE 274M [2] 27
88 2 768 44 148 P 4 5 36 P 1 SMPTE 274M [2] 27
113 2 998 44 148 P 4 5 11 P 1 SMPTE 274M [2] 27
93,103 2 1276 88 296 P 8 10 72 P 1 SMPTE 274M [2] 1,2,29
94,104 2 1056 88 296 P 8 10 72 P 1 SMPTE 274M [2] 1,2,29
95,105 2 176 88 296 P 8 10 72 P 1 SMPTE 274M [2] 1,2,29
114,116 2 1276 88 296 P 8 10 72 P 1 SMPTE 274M [2] 1,2,29
98 2 1020 88 296 P 8 10 72 P 1 SMPTE 274M [2] 1,2,30
99 2 968 88 128 P 8 10 72 P 1 SMPTE 274M [2] 1,2,30
100 2 88 88 128 P 8 10 72 P 1 SMPTE 274M [2] 1,2,30
115 2 1020 88 296 P 8 10 72 P 1 SMPTE 274M [2] 1,2,30
121 2 1996 88 296 P 8 10 22 P 1 SMPTE 274M [2] 31
122 2 1696 88 296 P 8 10 22 P 1 SMPTE 274M [2] 31
123 2 664 88 128 P 8 10 22 P 1 SMPTE 274M [2] 31
124 2 746 88 296 P 8 10 297 P 1 SMPTE 274M [2] 31
194,202 2 2552 176 592 P 16 20 144 P 1 SMPTE 274M [2]
195,203 2 2352 176 592 P 16 20 44 P 1 SMPTE 274M [2]
196,204 2 552 176 592 P 16 20 44 P 1 SMPTE 274M [2]
197,205 2 2552 176 592 P 16 20 144 P 1 SMPTE 274M [2]
210 2 1492 176 592 P 16 20 594 P 1 SMPTE 274M [2] 32
211 2 2492 176 592 P 16 20 44 P 1 SMPTE 274M [2] 32
212 2 288 176 296 P 16 20 144 P 1 SMPTE 274M [2] 32
213 2 1492 176 592 P 16 20 594 P 1 SMPTE 274M [2] 32

17,18 1 12 64 68 N 5 5 39 N 1 ITU-R BT.1358 [77]
19,68 2 440 40 220 P 5 5 20 P 1 SMPTE 296M [61] 1,2
20 4 528 44 148 P 2 5 15 P 1 SMPTE 274M [2] 1,2
21,22 3 24 126 138 N 2 3 19 N 1 ITU-R BT.656 [75] 6,15
23,24 1 24 126 138 N 2 3 19 N 1 ITU-R BT.1358 [77] 7,14,15,19
23,24 1 24 126 138 N 3 3 19 N 1 ITU-R BT.1358 [77] 7,14,15,19
23,24 1 24 126 138 N 4 3 19 N 1 ITU-R BT.1358 [77] 7,14,15,19
25,26 3 48 252 276 N 2 3 19 N 1 ITU-R BT.656 [75] 8,13,14
27,28 1 48 252 276 N 2 3 19 N 1 ITU-R BT.656 [75] 7,8,12,13,19
27,28 1 48 252 276 N 3 3 19 N 1 ITU-R BT.656 [75] 7,8,12,13,19
27,28 1 48 252 276 N 4 3 19 N 1 ITU-R BT.656 [75] 7,8,12,13,19
29,30 1 24 128 136 N 5 5 39 N 1 ITU-R BT.1358 [77] 9,10,14
31,75 2 528 44 148 P 4 5 36 P 1 SMPTE 274M [2] 14
37,38 1 48 256 272 N 5 5 39 N 1 ITU-R BT.1358 [77] 9,11
39 5 32 168 184 P 23 5 57 N 1 AS 4933.1-2005 [88] 5
82 2 260 40 220 P 5 5 20 P 1 SMPTE 296M [61] 1,2,26
89 2 548 44 148 P 4 5 36 P 1 SMPTE 274M [2] 14,27
96,106 2 1056 88 296 P 8 10 72 P 1 SMPTE 274M [2] 1,2,29
101 2 968 88 128 P 8 10 72 P 1 SMPTE 274M [2] 1,2,30
125 2 1096 88 296 P 8 10 72 P 1 SMPTE 274M [2] 31
198,206 2 2352 176 592 P 16 20 44 P 1 SMPTE 274M [2]
214 2 2492 176 592 P 16 20 44 P 1 SMPTE 274M [2] 32

1 1 16 96 48 N 10 2 33 N 1 VESA DMT [86] 3,4
2,3 1 16 62 60 N 9 6 30 N 7 CTA-770.2 [30] 2
4,69 2 110 40 220 P 5 5 20 P 1 CTA-770.3 [31] 1,2
5 4 88 44 148 P 2 5 15 P 1 CTA-770.3 [31] 1,2
6,7 3 38 124 114 N 4 3 15 N 4 CTA-770.2 [30] 2,15
8,9 1 38 124 114 N 4 3 15 N 4 CTA-770.2 [30] 7,14,15,19
8,9 1 38 124 114 N 5 3 15 N 4 CTA-770.2 [30] 7,14,15,19
10,11 3 76 248 228 N 4 3 15 N 4 CTA-770.2 [30] 8,13
12,13 1 76 248 228 N 4 3 15 N 4 CTA-770.2 [30] 7,8,13,19
12,13 1 76 248 228 N 5 3 15 N 4 CTA-770.2 [30] 7,8,13,19
14,15 1 32 124 120 N 9 6 30 N 7 CTA-770.2 [30] 9,10,13,14
16,76 2 88 44 148 P 4 5 36 P 1 SMPTE 274M [2] 14
35,36 1 64 248 240 N 9 6 30 N 7 CTA-770.2 [30] 9,11
83 2 260 40 220 P 5 5 20 P 1 CTA-770.3[31] 1,2,26
90 2 248 44 148 P 4 5 11 P 1 SMPTE 274M [2] 14,27
97,107 2 176 88 296 P 8 10 72 P 1 SMPTE 274M [2] 1,2,29
102 2 88 88 128 P 8 10 72 P 1 SMPTE 274M [2] 1,2,30
126 2 164 88 128 P 8 10 72 P 1 SMPTE 274M [2] 31
199,207 2 552 176 592 P 16 20 44 P 1 SMPTE 274M [2]
215 2 288 176 296 P 16 20 144 P 1 SMPTE 274M [2] 32

40 4 528 44 148 P 2 5 15 P 1 SMPTE 274M [2]
41,70 2 440 40 220 P 5 5 20 P 1 SMPTE 296M [61]
42,43 1 12 64 68 N 5 5 39 N 1 ITU-R BT.1358 [77]
44,45 3 24 126 138 N 2 3 19 N 1 ITU-R BT.656 [75] 16
64,77 2 528 44 148 P 4 5 36 P 1 SMPTE 274M [2]
84 2 60 40 220 P 5 5 95 P 1 SMPTE 296M [61] 26
91 2 218 44 148 P 4 5 161 P 1 SMPTE 274M [2] 27
117,119 2 1056 88 296 P 8 10 72 P 1 SMPTE 274M [2]
127 2 1096 88 296 P 8 10 72 P 1 SMPTE 274M [2] 31
200,208 2 2112 176 592 P 16 20 144 P 1 SMPTE 274M [2]
216 2 2192 176 592 P 16 20 144 P 1 SMPTE 274M [2] 32
218 2 800 88 296 P 8 10 72 P 1 SMPTE 274M [2]
46 4 88 44 148 P 2 5 15 P 1 SMPTE 274M [2]
47,71 2 110 40 220 P 5 5 20 P 1 SMPTE 296M [61]
48,49 1 16 62 60 N 9 6 30 N 7 CTA-770.2 [30]
50,51 3 38 124 114 N 4 3 15 N 4 CTA-770.2 [30] 16
63,78 2 88 44 148 P 4 5 36 P 1 SMPTE 274M [2]
85 2 60 40 220 P 5 5 95 P 1 SMPTE 296M [61] 26
92 2 548 44 148 P 4 5 161 P 1 SMPTE 274M [2] 27
118,120 2 176 88 296 P 8 10 72 P 1 SMPTE 274M [2]
193 2 164 88 128 P 8 10 72 P 1 SMPTE 274M [2] 31
201,209 2 352 176 592 P 16 20 144 P 1 SMPTE 274M [2]
217 2 288 176 296 P 16 20 144 P 1 SMPTE 274M [2] 32
219 2 88 88 128 P 8 10 72 P 1 SMPTE 274M [2]
52,53 1 12 64 68 N 5 5 39 N 1 ITU-R BT.1358 [77]
54,55 3 24 126 138 N 2 3 19 N 1 ITU-R BT.656 [75] 16
56,57 1 16 62 60 N 9 6 30 N 7 CTA-770.2 [30]
58,59 3 38 124 114 N 4 3 15 N 4 CTA-770.2 [30] 16
"""

# Table 3 Video Formats--Video ID Code and Aspect Ratios
CTA_861_TABLE_3 = """
1 640x480p 59.94Hz/60Hz 4:3 1:1
2 720x480p 59.94Hz/60Hz 4:3 8:9
3 720x480p 59.94Hz/60Hz 16:9 32:27
4 1280x720p 59.94Hz/60Hz 16:9 1:1
5 1920x1080i 59.94Hz/60Hz 16:9 1:1
6 720(1440)x480i 59.94Hz/60Hz 4:3 8:9
7 720(1440)x480i 59.94Hz/60Hz 16:9 32:27
8 720(1440)x240p 59.94Hz/60Hz 4:3 4:9
9 720(1440)x240p 59.94Hz/60Hz 16:9 16:27
10 2880x480i 59.94Hz/60Hz 4:3 2:9 – 20:9
11 2880x480i 59.94Hz/60Hz 16:9 8:27 -80:27
12 2880x240p 59.94Hz/60Hz 4:3 1:9 – 10:9
13 2880x240p 59.94Hz/60Hz 16:9 4:27 – 40:27
14 1440x480p 59.94Hz/60Hz 4:3 4:9 or 8:9
15 1440x480p 59.94Hz/60Hz 16:9 16:27 or 32:27
16 1920x1080p 59.94Hz/60Hz 16:9 1:1
17 720x576p 50Hz 4:3 16:15
18 720x576p 50Hz 16:9 64:45
19 1280x720p 50Hz 16:9 1:1
20 1920x1080i 50Hz 16:9 1:1
21 720(1440)x576i 50Hz 4:3 16:15
22 720(1440)x576i 50Hz 16:9 64:45
23 720(1440)x288p 50Hz 4:3 8:15
24 720(1440)x288p 50Hz 16:9 32:45
25 2880x576i 50Hz 4:3 2:15 – 20:15
26 2880x576i 50Hz 16:9 16:45-160:45
27 2880x288p 50Hz 4:3 1:15 – 10:15
28 2880x288p 50Hz 16:9 8:45 – 80:45
29 1440x576p 50Hz 4:3 8:15 or 16:15
30 1440x576p 50Hz 16:9 32:45 or 64:45
31 1920x1080p 50Hz 16:9 1:1
32 1920x1080p 23.98Hz/24Hz 16:9 1:1
33 1920x1080p 25Hz 16:9 1:1
34 1920x1080p 29.97Hz/30Hz 16:9 1:1
35 2880x480p 59.94Hz/60Hz 4:3 2:9,4:9,or 8:9
36 2880x480p 59.94Hz/60Hz 16:9 8:27,16:27,or 32:27
37 2880x576p 50Hz 4:3 4:15,8:15,or 16:15
38 2880x576p 50Hz 16:9 16:45,32:45,or 64:45
39 1920x1080i 50Hz 16:9 1:1
40 1920x1080i 100Hz 16:9 1:1
41 1280x720p 100Hz 16:9 1:1
42 720x576p 100Hz 4:3 16:15
43 720x576p 100Hz 16:9 64:45
44 720(1440)x576i 100Hz 4:3 16:15
45 720(1440)x576i 100Hz 16:9 64:45
46 1920x1080i 119.88/120Hz 16:9 1:1
47 1280x720p 119.88/120Hz 16:9 1:1
48 720x480p 119.88/120Hz 4:3 8:9
49 720x480p 119.88/120Hz 16:9 32:27
50 720(1440)x480i 119.88/120Hz 4:3 8:9
51 720(1440)x480i 119.88/120Hz 16:9 32:27
52 720x576p 200Hz 4:3 16:15
53 720x576p 200Hz 16:9 64:45
54 720(1440)x576i 200Hz 4:3 16:15
55 720(1440)x576i 200Hz 16:9 64:45
56 720x480p 239.76/240Hz 4:3 8:9
57 720x480p 239.76/240Hz 16:9 32:27
58 720(1440)x480i 239.76/240Hz 4:3 8:9
59 720(1440)x480i 239.76/240Hz 16:9 32:27
60 1280x720p 23.98Hz/24Hz 16:9 1:1
61 1280x720p 25Hz 16:9 1:1
62 1280x720p 29.97Hz/30Hz 16:9 1:1
63 1920x1080p 119.88/120Hz 16:9 1:1
64 1920x1080p 100Hz 16:9 1:1
65 1280x720p 23.98Hz/24Hz 64:27 4:3
66 1280x720p 25Hz 64:27 4:3
67 1280x720p 29.97Hz/30Hz 64:27 4:3
68 1280x720p 50Hz 64:27 4:3
69 1280x720p 59.94Hz/60Hz 64:27 4:3
70 1280x720p 100Hz 64:27 4:3
71 1280x720p 119.88/120Hz 64:27 4:3
72 1920x1080p 23.98Hz/24Hz 64:27 4:3
73 1920x1080p 25Hz 64:27 4:3
74 1920x1080p 29.97Hz/30Hz 64:27 4:3
75 1920x1080p 50Hz 64:27 4:3
76 1920x1080p 59.94Hz/60Hz 64:27 4:3
77 1920x1080p 100Hz 64:27 4:3
78 1920x1080p 119.88/120Hz 64:27 4:3
79 1680x720p 23.98Hz/24Hz 64:27 64:63
80 1680x720p 25Hz 64:27 64:63
81 1680x720p 29.97Hz/30Hz 64:27 64:63
82 1680x720p 50Hz 64:27 64:63
83 1680x720p 59.94Hz/60Hz 64:27 64:63
84 1680x720p 100Hz 64:27 64:63
85 1680x720p 119.88/120Hz 64:27 64:63
86 2560x1080p 23.98Hz/24Hz 64:27 1:1
87 2560x1080p 25Hz 64:27 1:1
88 2560x1080p 29.97Hz/30Hz 64:27 1:1
89 2560x1080p 50Hz 64:27 1:1
90 2560x1080p 59.94Hz/60Hz 64:27 1:1
91 2560x1080p 100Hz 64:27 1:1
92 2560x1080p 119.88/120Hz 64:27 1:1
93 3840x2160p 23.98Hz/24Hz 16:9 1:1
94 3840x2160p 25Hz 16:9 1:1
95 3840x2160p 29.97Hz/30Hz 16:9 1:1
96 3840x2160p 50Hz 16:9 1:1
97 3840x2160p 59.94Hz/60Hz 16:9 1:1
98 4096x2160p 23.98Hz/24Hz 256:135 1:1
99 4096x2160p 25Hz 256:135 1:1
100 4096x2160p 29.97Hz/30Hz 256:135 1:1
101 4096x2160p 50Hz 256:135 1:1
102 4096x2160p 59.94Hz/60Hz 256:135 1:1
103 3840x2160p 23.98Hz/24Hz 64:27 4:3
104 3840x2160p 25Hz 64:27 4:3
105 3840x2160p 29.97Hz/30Hz 64:27 4:3
106 3840x2160p 50Hz 64:27 4:3
107 3840x2160p 59.94Hz/60Hz 64:27 4:3
108 1280x720p 47.95Hz/48Hz 16:9 1:1
109 1280x720p 47.95Hz/48Hz 64:27 4:3
110 1680x720p 47.95Hz/48Hz 64:27 64:63
111 1920x1080p 47.95Hz/48Hz 16:9 1:1
112 1920x1080p 47.95Hz/48Hz 64:27 4:3
113 2560x1080p 47.95Hz/48Hz 64:27 1:1
114 3840x2160p 47.95Hz/48Hz 16:9 1:1
115 4096x2160p 47.95Hz/48Hz 256:135 1:1
116 3840x2160p 47.95Hz/48Hz 64:27 4:3
117 3840x2160p 100Hz 16:9 1:1
118 3840x2160p 119.88/120Hz 16:9 1:1
119 3840x2160p 100Hz 64:27 4:3
120 3840x2160p 119.88/120Hz 64:27 4:3
121 5120x2160p 23.98Hz/24Hz 64:27 1:1
122 5120x2160p 25Hz 64:27 1:1
123 5120x2160p 29.97Hz/30Hz 64:27 1:1
124 5120x2160p 47.95Hz/48Hz 64:27 1:1
125 5120x2160p 50Hz 64:27 1:1
126 5120x2160p 59.94Hz/60Hz 64:27 1:1
127 5120x2160p 100Hz 64:27 1:1
128-192 Forbidden
193 5120x2160p 119.88/120Hz 64:27 1:1
194 7680x4320p 23.98Hz/24Hz 16:9 1:1
195 7680x4320p 25Hz 16:9 1:1
196 7680x4320p 29.97Hz/30Hz 16:9 1:1
197 7680x4320p 47.95Hz/48Hz 16:9 1:1
198 7680x4320p 50Hz 16:9 1:1
199 7680x4320p 59.94Hz/60Hz 16:9 1:1
200 7680x4320p 100Hz 16:9 1:1
201 7680x4320p 119.88/120Hz 16:9 1:1
202 7680x4320p 23.98Hz/24Hz 64:27 4:3
203 7680x4320p 25Hz 64:27 4:3
204 7680x4320p 29.97Hz/30Hz 64:27 4:3
205 7680x4320p 47.95Hz/48Hz 64:27 4:3
206 7680x4320p 50Hz 64:27 4:3
207 7680x4320p 59.94Hz/60Hz 64:27 4:3
208 7680x4320p 100Hz 64:27 4:3
209 7680x4320p 119.88/120Hz 64:27 4:3
210 10240x4320p 23.98Hz/24Hz 64:27 1:1
211 10240x4320p 25Hz 64:27 1:1
212 10240x4320p 29.97Hz/30Hz 64:27 1:1
213 10240x4320p 47.95Hz/48Hz 64:27 1:1
214 10240x4320p 50Hz 64:27 1:1
215 10240x4320p 59.94Hz/60Hz 64:27 1:1
216 10240x4320p 100Hz 64:27 1:1
217 10240x4320p 119.88/120Hz 64:27 1:1
218 4096x2160p 100Hz 256:135 1:1
219 4096x2160p 119.88/120Hz 256:135 1:1
220-255 Reserved for the Future
0 No Video Identification Code Available (Used with AVI InfoFrame only)
"""

#
# VESA DMT
#
# https://app.box.com/s/vcocw3z73ta09txiskj7cnk6289j356b/file/93518546259
# (mode timing pages extracted to TSV with Tabula)
#

VESA_DMT_TABLE = """
  Detailed Timing Parameters  
Timing Name = 640 x 350 @ 85Hz;  
Hor Pixels = 640; // Pixels  
Ver Pixels = 350; // Lines  
Hor Frequency = 37.861; // kHz = 26.4 usec /  line
Ver Frequency = 85.080; // Hz = 11.8 msec /  frame
Pixel Clock = 31.500; // MHz = 31.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 254.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = POSITIVE; // HBlank = 23.1% of HTotal  
Ver Sync Polarity = NEGATIVE; // VBlank = 21.3% of VTotal  
Hor Total Time = 26.413; // (usec) = 104 chars =  832 Pixels
Hor Addr Time = 20.317; // (usec) = 80 chars =  640 Pixels
Hor Blank Start = 20.317; // (usec) = 80 chars =  640 Pixels
Hor Blank Time = 6.095; // (usec) = 24 chars =  192 Pixels
Hor Sync Start = 21.333; // (usec) = 84 chars =  672 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 1.016; // (usec) = 4 chars =  32 Pixels
Hor Sync Time = 2.032; // (usec) = 8 chars =  64 Pixels
// H Back Porch = 3.048; // (usec) = 12 chars =  96 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.754; // (msec) = 445 lines  HT – (1.06xHA)
Ver Addr Time = 9.244; // (msec) = 350 lines  = 4.88
Ver Blank Start = 9.244; // (msec) = 350 lines  
Ver Blank Time = 2.509; // (msec) = 95 lines  
Ver Sync Start = 10.090; // (msec) = 382 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.845; // (msec) = 32 lines  
Ver Sync Time = 0.079; // (msec) = 3 lines  
// V Back Porch = 1.585; // (msec) = 60 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 640 x 400 @ 85Hz;  
Hor Pixels = 640; // Pixels  
Ver Pixels = 400; // Lines  
Hor Frequency = 37.861; // kHz = 26.4 usec /  line
Ver Frequency = 85.080; // Hz = 11.8 msec /  frame
Pixel Clock = 31.500; // MHz = 31.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 254.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = NEGATIVE; // HBlank = 23.1% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 10.1% of VTotal  
Hor Total Time = 26.413; // (usec) = 104 chars =  832 Pixels
Hor Addr Time = 20.317; // (usec) = 80 chars =  640 Pixels
Hor Blank Start = 20.317; // (usec) = 80 chars =  640 Pixels
Hor Blank Time = 6.095; // (usec) = 24 chars =  192 Pixels
Hor Sync Start = 21.333; // (usec) = 84 chars =  672 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 1.016; // (usec) = 4 chars =  32 Pixels
Hor Sync Time = 2.032; // (usec) = 8 chars =  64 Pixels
// H Back Porch = 3.048; // (usec) = 12 chars =  96 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.754; // (msec) = 445 lines  HT – (1.06xHA)
Ver Addr Time = 10.565; // (msec) = 400 lines  = 4.88
Ver Blank Start = 10.565; // (msec) = 400 lines  
Ver Blank Time = 1.189; // (msec) = 45 lines  
Ver Sync Start = 10.591; // (msec) = 401 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.026; // (msec) = 1 lines  
Ver Sync Time = 0.079; // (msec) = 3 lines  
// V Back Porch = 1.083; // (msec) = 41 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 720 x 400 @ 85Hz;  
Hor Pixels = 720; // Pixels  
Ver Pixels = 400; // Lines  
Hor Frequency = 37.927; // kHz = 26.4 usec /  line
Ver Frequency = 85.039; // Hz = 11.8 msec /  frame
Pixel Clock = 35.500; // MHz = 28.2 nsec  ± 0.5%
Character Width = 9; // Pixels = 253.5 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = NEGATIVE; // HBlank = 23.1% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 10.3% of VTotal  
Hor Total Time = 26.366; // (usec) = 104 chars =  936 Pixels
Hor Addr Time = 20.282; // (usec) = 80 chars =  720 Pixels
Hor Blank Start = 20.282; // (usec) = 80 chars =  720 Pixels
Hor Blank Time = 6.085; // (usec) = 24 chars =  216 Pixels
Hor Sync Start = 21.296; // (usec) = 84 chars =  756 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 1.014; // (usec) = 4 chars =  36 Pixels
Hor Sync Time = 2.028; // (usec) = 8 chars =  72 Pixels
// H Back Porch = 3.042; // (usec) = 12 chars =  108 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.759; // (msec) = 446 lines  HT – (1.06xHA)
Ver Addr Time = 10.546; // (msec) = 400 lines  = 4.87
Ver Blank Start = 10.546; // (msec) = 400 lines  
Ver Blank Time = 1.213; // (msec) = 46 lines  
Ver Sync Start = 10.573; // (msec) = 401 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.026; // (msec) = 1 lines  
Ver Sync Time = 0.079; // (msec) = 3 lines  
// V Back Porch = 1.107; // (msec) = 42 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 640 x 480 @ 60Hz;  
Hor Pixels = 640; // Pixels  
Ver Pixels = 480; // Lines  
Hor Frequency = 31.469; // kHz = 31.8 usec /  line
Ver Frequency = 59.940; // Hz = 16.7 msec /  frame
Pixel Clock = 25.175; // MHz = 39.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 317.8 nsec  
Scan Type = NONINTERLACED; // H Phase =  2.0 %
Hor Sync Polarity = NEGATIVE; // HBlank = 18.0% of HTotal  
Ver Sync Polarity = NEGATIVE; // VBlank = 5.5% of VTotal  
Hor Total Time = 31.778; // (usec) = 100 chars =  800 Pixels
Hor Addr Time = 25.422; // (usec) = 80 chars =  640 Pixels
Hor Blank Start = 25.740; // (usec) = 81 chars =  648 Pixels
Hor Blank Time = 5.720; // (usec) = 18 chars =  144 Pixels
Hor Sync Start = 26.058; // (usec) = 82 chars =  656 Pixels
// H Right Border = 0.318; // (usec) = 1 chars =  8 Pixels
// H Front Porch = 0.318; // (usec) = 1 chars =  8 Pixels
Hor Sync Time = 3.813; // (usec) = 12 chars =  96 Pixels
// H Back Porch = 1.589; // (usec) = 5 chars =  40 Pixels
// H Left Border = 0.318; // (usec) = 1 chars =  8 Pixels
Ver Total Time = 16.683; // (msec) = 525 lines  HT – (1.06xHA)
Ver Addr Time = 15.253; // (msec) = 480 lines  = 4.83
Ver Blank Start = 15.507; // (msec) = 488 lines  
Ver Blank Time = 0.922; // (msec) = 29 lines  
Ver Sync Start = 15.571; // (msec) = 490 lines  
// V Bottom Border = 0.254; // (msec) = 8 lines  
// V Front Porch = 0.064; // (msec) = 2 lines  
Ver Sync Time = 0.064; // (msec) = 2 lines  
// V Back Porch = 0.794; // (msec) = 25 lines  
// V Top Border = 0.254; // (msec) = 8 lines  
  Detailed Timing Parameters  
Timing Name = 640 x 480 @ 72Hz;  
Hor Pixels = 640; // Pixels  
Ver Pixels = 480; // Lines  
Hor Frequency = 37.861; // kHz = 26.4 usec /  line
Ver Frequency = 72.809; // Hz = 13.7 msec /  frame
Pixel Clock = 31.500; // MHz = 31.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 254.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  6.3 %
Hor Sync Polarity = NEGATIVE; // HBlank = 21.2% of HTotal  
Ver Sync Polarity = NEGATIVE; // VBlank = 4.6% of VTotal  
Hor Total Time = 26.413; // (usec) = 104 chars =  832 Pixels
Hor Addr Time = 20.317; // (usec) = 80 chars =  640 Pixels
Hor Blank Start = 20.571; // (usec) = 81 chars =  648 Pixels
Hor Blank Time = 5.587; // (usec) = 22 chars =  176 Pixels
Hor Sync Start = 21.079; // (usec) = 83 chars =  664 Pixels
// H Right Border = 0.254; // (usec) = 1 chars =  8 Pixels
// H Front Porch = 0.508; // (usec) = 2 chars =  16 Pixels
Hor Sync Time = 1.270; // (usec) = 5 chars =  40 Pixels
// H Back Porch = 3.810; // (usec) = 15 chars =  120 Pixels
// H Left Border = 0.254; // (usec) = 1 chars =  8 Pixels
Ver Total Time = 13.735; // (msec) = 520 lines  HT – (1.06xHA)
Ver Addr Time = 12.678; // (msec) = 480 lines  = 4.88
Ver Blank Start = 12.889; // (msec) = 488 lines  
Ver Blank Time = 0.634; // (msec) = 24 lines  
Ver Sync Start = 12.916; // (msec) = 489 lines  
// V Bottom Border = 0.211; // (msec) = 8 lines  
// V Front Porch = 0.026; // (msec) = 1 lines  
Ver Sync Time = 0.079; // (msec) = 3 lines  
// V Back Porch = 0.528; // (msec) = 20 lines  
// V Top Border = 0.211; // (msec) = 8 lines  
  Detailed Timing Parameters  
Timing Name = 640 x 480 @ 75Hz;  
Hor Pixels = 640; // Pixels  
Ver Pixels = 480; // Lines  
Hor Frequency = 37.500; // kHz = 26.7 usec /  line
Ver Frequency = 75.000; // Hz = 13.3 msec /  frame
Pixel Clock = 31.500; // MHz = 31.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 254.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  6.2 %
Hor Sync Polarity = NEGATIVE; // HBlank = 23.8% of HTotal  
Ver Sync Polarity = NEGATIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 26.667; // (usec) = 105 chars =  840 Pixels
Hor Addr Time = 20.317; // (usec) = 80 chars =  640 Pixels
Hor Blank Start = 20.317; // (usec) = 80 chars =  640 Pixels
Hor Blank Time = 6.349; // (usec) = 25 chars =  200 Pixels
Hor Sync Start = 20.825; // (usec) = 82 chars =  656 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.508; // (usec) = 2 chars =  16 Pixels
Hor Sync Time = 2.032; // (usec) = 8 chars =  64 Pixels
// H Back Porch = 3.810; // (usec) = 15 chars =  120 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.333; // (msec) = 500 lines  HT – (1.06xHA)
Ver Addr Time = 12.800; // (msec) = 480 lines  = 5.13
Ver Blank Start = 12.800; // (msec) = 480 lines  
Ver Blank Time = 0.533; // (msec) = 20 lines  
Ver Sync Start = 12.827; // (msec) = 481 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.027; // (msec) = 1 lines  
Ver Sync Time = 0.080; // (msec) = 3 lines  
// V Back Porch = 0.427; // (msec) = 16 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 640 x 480 @ 85Hz;  
Hor Pixels = 640; // Pixels  
Ver Pixels = 480; // Lines  
Hor Frequency = 43.269; // kHz = 23.1 usec /  line
Ver Frequency = 85.008; // Hz = 11.8 msec /  frame
Pixel Clock = 36.000; // MHz = 27.8 nsec  ± 0.5%
Character Width = 8; // Pixels = 222.2 nsec  
Scan Type = NONINTERLACED; // H Phase =  1.4 %
Hor Sync Polarity = NEGATIVE; // HBlank = 23.1% of HTotal  
Ver Sync Polarity = NEGATIVE; // VBlank = 5.7% of VTotal  
Hor Total Time = 23.111; // (usec) = 104 chars =  832 Pixels
Hor Addr Time = 17.778; // (usec) = 80 chars =  640 Pixels
Hor Blank Start = 17.778; // (usec) = 80 chars =  640 Pixels
Hor Blank Time = 5.333; // (usec) = 24 chars =  192 Pixels
Hor Sync Start = 19.333; // (usec) = 87 chars =  696 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 1.556; // (usec) = 7 chars =  56 Pixels
Hor Sync Time = 1.556; // (usec) = 7 chars =  56 Pixels
// H Back Porch = 2.222; // (usec) = 10 chars =  80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.764; // (msec) = 509 lines  HT – (1.06xHA)
Ver Addr Time = 11.093; // (msec) = 480 lines  = 4.27
Ver Blank Start = 11.093; // (msec) = 480 lines  
Ver Blank Time = 0.670; // (msec) = 29 lines  
Ver Sync Start = 11.116; // (msec) = 481 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.023; // (msec) = 1 lines  
Ver Sync Time = 0.069; // (msec) = 3 lines  
// V Back Porch = 0.578; // (msec) = 25 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 800 x 600 @ 56Hz;  
Hor Pixels = 800; // Pixels  
Ver Pixels = 600; // Lines  
Hor Frequency = 35.156; // kHz = 28.4 usec /  line
Ver Frequency = 56.250; // Hz = 17.8 msec /  frame
Pixel Clock = 36.000; // MHz = 27.8 nsec  ± 0.5%
Character Width = 8; // Pixels = 222.2 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 21.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 28.444; // (usec) = 128 chars =  1024 Pixels
Hor Addr Time = 22.222; // (usec) = 100 chars =  800 Pixels
Hor Blank Start = 22.222; // (usec) = 100 chars =  800 Pixels
Hor Blank Time = 6.222; // (usec) = 28 chars =  224 Pixels
Hor Sync Start = 22.889; // (usec) = 103 chars =  824 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.667; // (usec) = 3 chars =  24 Pixels
Hor Sync Time = 2.000; // (usec) = 9 chars =  72 Pixels
// H Back Porch = 3.556; // (usec) = 16 chars =  128 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 17.778; // (msec) = 625 lines  HT – (1.06xHA)
Ver Addr Time = 17.067; // (msec) = 600 lines  = 4.89
Ver Blank Start = 17.067; // (msec) = 600 lines  
Ver Blank Time = 0.711; // (msec) = 25 lines  
Ver Sync Start = 17.095; // (msec) = 601 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.028; // (msec) = 1 lines  
Ver Sync Time = 0.057; // (msec) = 2 lines  
// V Back Porch = 0.626; // (msec) = 22 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 800 x 600 @ 60Hz;  
Hor Pixels = 800; // Pixels  
Ver Pixels = 600; // Lines  
Hor Frequency = 37.879; // kHz = 26.4 usec /  line
Ver Frequency = 60.317; // Hz = 16.6 msec /  frame
Pixel Clock = 40.000; // MHz = 25.0 nsec  ± 0.5%
Character Width = 8; // Pixels = 200.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  2.3 %
Hor Sync Polarity = POSITIVE; // HBlank = 24.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.5% of VTotal  
Hor Total Time = 26.400; // (usec) = 132 chars =  1056 Pixels
Hor Addr Time = 20.000; // (usec) = 100 chars =  800 Pixels
Hor Blank Start = 20.000; // (usec) = 100 chars =  800 Pixels
Hor Blank Time = 6.400; // (usec) = 32 chars =  256 Pixels
Hor Sync Start = 21.000; // (usec) = 105 chars =  840 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 1.000; // (usec) = 5 chars =  40 Pixels
Hor Sync Time = 3.200; // (usec) = 16 chars =  128 Pixels
// H Back Porch = 2.200; // (usec) = 11 chars =  88 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.579; // (msec) = 628 lines  HT – (1.06xHA)
Ver Addr Time = 15.840; // (msec) = 600 lines  = 5.2
Ver Blank Start = 15.840; // (msec) = 600 lines  
Ver Blank Time = 0.739; // (msec) = 28 lines  
Ver Sync Start = 15.866; // (msec) = 601 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.026; // (msec) = 1 lines  
Ver Sync Time = 0.106; // (msec) = 4 lines  
// V Back Porch = 0.607; // (msec) = 23 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 800 x 600 @ 72Hz;  
Hor Pixels = 800; // Pixels  
Ver Pixels = 600; // Lines  
Hor Frequency = 48.077; // kHz = 20.8 usec /  line
Ver Frequency = 72.188; // Hz = 13.9 msec /  frame
Pixel Clock = 50.000; // MHz = 20.0 nsec  ± 0.5%
Character Width = 8; // Pixels = 160.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  0.4 %
Hor Sync Polarity = POSITIVE; // HBlank = 23.1% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 9.9% of VTotal  
Hor Total Time = 20.800; // (usec) = 130 chars =  1040 Pixels
Hor Addr Time = 16.000; // (usec) = 100 chars =  800 Pixels
Hor Blank Start = 16.000; // (usec) = 100 chars =  800 Pixels
Hor Blank Time = 4.800; // (usec) = 30 chars =  240 Pixels
Hor Sync Start = 17.120; // (usec) = 107 chars =  856 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 1.120; // (usec) = 7 chars =  56 Pixels
Hor Sync Time = 2.400; // (usec) = 15 chars =  120 Pixels
// H Back Porch = 1.280; // (usec) = 8 chars =  64 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.853; // (msec) = 666 lines  HT – (1.06xHA)
Ver Addr Time = 12.480; // (msec) = 600 lines  = 3.84
Ver Blank Start = 12.480; // (msec) = 600 lines  
Ver Blank Time = 1.373; // (msec) = 66 lines  
Ver Sync Start = 13.250; // (msec) = 637 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.770; // (msec) = 37 lines  
Ver Sync Time = 0.125; // (msec) = 6 lines  
// V Back Porch = 0.478; // (msec) = 23 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 800 x 600 @ 75Hz;  
Hor Pixels = 800; // Pixels  
Ver Pixels = 600; // Lines  
Hor Frequency = 46.875; // kHz = 21.3 usec /  line
Ver Frequency = 75.000; // Hz = 13.3 msec /  frame
Pixel Clock = 49.500; // MHz = 20.2 nsec  ± 0.5%
Character Width = 8; // Pixels = 161.6 nsec  
Scan Type = NONINTERLACED; // H Phase =  6.8 %
Hor Sync Polarity = POSITIVE; // HBlank = 24.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 21.333; // (usec) = 132 chars =  1056 Pixels
Hor Addr Time = 16.162; // (usec) = 100 chars =  800 Pixels
Hor Blank Start = 16.162; // (usec) = 100 chars =  800 Pixels
Hor Blank Time = 5.172; // (usec) = 32 chars =  256 Pixels
Hor Sync Start = 16.485; // (usec) = 102 chars =  816 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.323; // (usec) = 2 chars =  16 Pixels
Hor Sync Time = 1.616; // (usec) = 10 chars =  80 Pixels
// H Back Porch = 3.232; // (usec) = 20 chars =  160 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.333; // (msec) = 625 lines  HT – (1.06xHA)
Ver Addr Time = 12.800; // (msec) = 600 lines  = 4.2
Ver Blank Start = 12.800; // (msec) = 600 lines  
Ver Blank Time = 0.533; // (msec) = 25 lines  
Ver Sync Start = 12.821; // (msec) = 601 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.021; // (msec) = 1 lines  
Ver Sync Time = 0.064; // (msec) = 3 lines  
// V Back Porch = 0.448; // (msec) = 21 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 800 x 600 @ 85Hz;  
Hor Pixels = 800; // Pixels  
Ver Pixels = 600; // Lines  
Hor Frequency = 53.674; // kHz = 18.6 usec /  line
Ver Frequency = 85.061; // Hz = 11.8 msec /  frame
Pixel Clock = 56.250; // MHz = 17.8 nsec  ± 0.5%
Character Width = 8; // Pixels = 142.2 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.7 %
Hor Sync Polarity = POSITIVE; // HBlank = 23.7% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.9% of VTotal  
Hor Total Time = 18.631; // (usec) = 131 chars =  1048 Pixels
Hor Addr Time = 14.222; // (usec) = 100 chars =  800 Pixels
Hor Blank Start = 14.222; // (usec) = 100 chars =  800 Pixels
Hor Blank Time = 4.409; // (usec) = 31 chars =  248 Pixels
Hor Sync Start = 14.791; // (usec) = 104 chars =  832 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.569; // (usec) = 4 chars =  32 Pixels
Hor Sync Time = 1.138; // (usec) = 8 chars =  64 Pixels
// H Back Porch = 2.702; // (usec) = 19 chars =  152 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.756; // (msec) = 631 lines  HT – (1.06xHA)
Ver Addr Time = 11.179; // (msec) = 600 lines  = 3.56
Ver Blank Start = 11.179; // (msec) = 600 lines  
Ver Blank Time = 0.578; // (msec) = 31 lines  
Ver Sync Start = 11.197; // (msec) = 601 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.019; // (msec) = 1 lines  
Ver Sync Time = 0.056; // (msec) = 3 lines  
// V Back Porch = 0.503; // (msec) = 27 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 800 x 600 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 800; // Pixels
Ver Pixels = 600; // Lines
Hor Frequency = 76.302; // kHz = 13.1 usec / line
Ver Frequency = 119.972; // Hz = 8.3 msec / frame
Pixel Clock = 73.250; // MHz = 13.7 nsec ± 0.5%
Character Width = 8; // Pixels = 109.2 nsec
Scan Type = NONINTERLACED; // H Phase = 1.7 %
Hor Sync Polarity = POSITIVE; // HBlank = 16.7% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.7% of VTotal
Hor Total Time = 13.106; // (usec) = 120 chars = 960 Pixels
Hor Addr Time = 10.922; // (usec) = 100 chars = 800 Pixels
Hor Blank Start = 10.922; // (usec) = 100 chars = 800 Pixels
Hor Blank Time = 2.184; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 11.577; // (usec) = 106 chars = 848 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.655; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.437; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 1.092; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.335; // (msec) = 636 lines HT – (1.06xHA)
Ver Addr Time = 7.863; // (msec) = 600 lines = 1.53
Ver Blank Start = 7.863; // (msec) = 600 lines
Ver Blank Time = 0.472; // (msec) = 36 lines
Ver Sync Start = 7.903; // (msec) = 603 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.039; // (msec) = 3 lines
Ver Sync Time = 0.052; // (msec) = 4 lines
// V Back Porch = 0.380; // (msec) = 29 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 848 x 480 @ 60Hz;  
Hor Pixels = 848; // Pixels  
Ver Pixels = 480; // Lines  
Hor Frequency = 31.020; // kHz = 32.2 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 33.750; // MHz = 29.6 nsec  ± 0.5%
Character Width = 8; // Pixels = 237.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.4 %
Hor Sync Polarity = POSITIVE; // HBlank = 22.1% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 7.2% of VTotal  
Hor Total Time = 32.237; // (usec) = 136 chars =  1088 Pixels
Hor Addr Time = 25.126; // (usec) = 106 chars =  848 Pixels
Hor Blank Start = 25.126; // (usec) = 106 chars =  848 Pixels
Hor Blank Time = 7.111; // (usec) = 30 chars =  240 Pixels
Hor Sync Start = 25.600; // (usec) = 108 chars =  864 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.474; // (usec) = 2 chars =  16 Pixels
Hor Sync Time = 3.319; // (usec) = 14 chars =  112 Pixels
// H Back Porch = 3.319; // (usec) = 14 chars =  112 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 517 lines  HT – (1.06xHA)
Ver Addr Time = 15.474; // (msec) = 480 lines  = 5.6
Ver Blank Start = 15.474; // (msec) = 480 lines  
Ver Blank Time = 1.193; // (msec) = 37 lines  
Ver Sync Start = 15.667; // (msec) = 486 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.193; // (msec) = 6 lines  
Ver Sync Time = 0.258; // (msec) = 8 lines  
// V Back Porch = 0.741; // (msec) = 23 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters 
Timing Name = 1024 x 768 @ 43Hz (Interlaced); 
Hor Pixels = 1024; // Pixels 
Ver Pixels = 768; // Lines 
Hor Frequency = 35.522; // kHz = 28.2 usec / line
Ver Frequency = 86.957; // Hz = 11.5 msec / field
Pixel Clock = 44.900; // MHz = 22.3 nsec ± 0.5%
Character Width = 8; // Pixels = 178.2 nsec 
Scan Type = INTERLACED; 
Hor Sync Polarity = POSITIVE; // HBlank = 19.0% of HTotal 
Ver Sync Polarity = POSITIVE; // VBlank = 5.9% of VTotal 
Hor Total Time = 28.151; // (usec) = 158 chars = 1264 Pixels
Hor Addr Time = 22.806; // (usec) = 128 chars = 1024 Pixels
Hor Blank Start = 22.806; // (usec) = 128 chars = 1024 Pixels
Hor Blank Time = 5.345; // (usec) = 30 chars = 240 Pixels
Hor Sync Start = 22.984; // (usec) = 129 chars = 1032 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.178; // (usec) = 1 chars = 8 Pixels
Hor Sync Time = 3.920; // (usec) = 22 chars = 176 Pixels
// H Back Porch = 1.247; // (usec) = 7 chars = 56 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 23.000; // (msec) = 817 lines (Per Frame) 
Ver Addr Time = 21.620; // (msec) = 768 lines (Per Frame) 
Ver Blank Start = 21.620; // (msec) = 768 lines (Per Frame) 
Ver Blank Time = 0.676; // (msec) = 24 lines (Per Field) 
Ver Sync Start = 21.620; // (msec) = 768 lines (Per Frame) 
// V Bottom Border = 0.000; // (msec) = 0 lines (Odd Field) 
// V Front Porch = 0.000; // (msec) = 0 lines (Odd Field) 
Ver Sync Time = 0.113; // (msec) = 4 lines (Both Fields) 
// V Back Porch = 0.563; // (msec) = 20 lines (Odd Field) 
// V Top Border = 0.000; // (msec) = 0 lines (Odd Field) 
  Detailed Timing Parameters  
Timing Name = 1024 x 768 @ 60Hz;  
Hor Pixels = 1024; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 48.363; // kHz = 20.7 usec /  line
Ver Frequency = 60.004; // Hz = 16.7 msec /  frame
Pixel Clock = 65.000; // MHz = 15.4 nsec  ± 0.5%
Character Width = 8; // Pixels = 123.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.1 %
Hor Sync Polarity = NEGATIVE; // HBlank = 23.8% of HTotal  
Ver Sync Polarity = NEGATIVE; // VBlank = 4.7% of VTotal  
Hor Total Time = 20.677; // (usec) = 168 chars =  1344 Pixels
Hor Addr Time = 15.754; // (usec) = 128 chars =  1024 Pixels
Hor Blank Start = 15.754; // (usec) = 128 chars =  1024 Pixels
Hor Blank Time = 4.923; // (usec) = 40 chars =  320 Pixels
Hor Sync Start = 16.123; // (usec) = 131 chars =  1048 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.369; // (usec) = 3 chars =  24 Pixels
Hor Sync Time = 2.092; // (usec) = 17 chars =  136 Pixels
// H Back Porch = 2.462; // (usec) = 20 chars =  160 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.666; // (msec) = 806 lines  HT – (1.06xHA)
Ver Addr Time = 15.880; // (msec) = 768 lines  = 3.98
Ver Blank Start = 15.880; // (msec) = 768 lines  
Ver Blank Time = 0.786; // (msec) = 38 lines  
Ver Sync Start = 15.942; // (msec) = 771 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.062; // (msec) = 3 lines  
Ver Sync Time = 0.124; // (msec) = 6 lines  
// V Back Porch = 0.600; // (msec) = 29 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1024 x 768 @ 70Hz;  
Hor Pixels = 1024; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 56.476; // kHz = 17.7 usec /  line
Ver Frequency = 70.069; // Hz = 14.3 msec /  frame
Pixel Clock = 75.000; // MHz = 13.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 106.7 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.5 %
Hor Sync Polarity = NEGATIVE; // HBlank = 22.9% of HTotal  
Ver Sync Polarity = NEGATIVE; // VBlank = 4.7% of VTotal  
Hor Total Time = 17.707; // (usec) = 166 chars =  1328 Pixels
Hor Addr Time = 13.653; // (usec) = 128 chars =  1024 Pixels
Hor Blank Start = 13.653; // (usec) = 128 chars =  1024 Pixels
Hor Blank Time = 4.053; // (usec) = 38 chars =  304 Pixels
Hor Sync Start = 13.973; // (usec) = 131 chars =  1048 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.320; // (usec) = 3 chars =  24 Pixels
Hor Sync Time = 1.813; // (usec) = 17 chars =  136 Pixels
// H Back Porch = 1.920; // (usec) = 18 chars =  144 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 14.272; // (msec) = 806 lines  HT – (1.06xHA)
Ver Addr Time = 13.599; // (msec) = 768 lines  = 3.23
Ver Blank Start = 13.599; // (msec) = 768 lines  
Ver Blank Time = 0.673; // (msec) = 38 lines  
Ver Sync Start = 13.652; // (msec) = 771 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.053; // (msec) = 3 lines  
Ver Sync Time = 0.106; // (msec) = 6 lines  
// V Back Porch = 0.513; // (msec) = 29 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1024 x 768 @ 75Hz;  
Hor Pixels = 1024; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 60.023; // kHz = 16.7 usec /  line
Ver Frequency = 75.029; // Hz = 13.3 msec /  frame
Pixel Clock = 78.750; // MHz = 12.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 101.6 nsec  
Scan Type = NONINTERLACED; // H Phase =  6.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 22.0% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 16.660; // (usec) = 164 chars =  1312 Pixels
Hor Addr Time = 13.003; // (usec) = 128 chars =  1024 Pixels
Hor Blank Start = 13.003; // (usec) = 128 chars =  1024 Pixels
Hor Blank Time = 3.657; // (usec) = 36 chars =  288 Pixels
Hor Sync Start = 13.206; // (usec) = 130 chars =  1040 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.203; // (usec) = 2 chars =  16 Pixels
Hor Sync Time = 1.219; // (usec) = 12 chars =  96 Pixels
// H Back Porch = 2.235; // (usec) = 22 chars =  176 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.328; // (msec) = 800 lines  HT – (1.06xHA)
Ver Addr Time = 12.795; // (msec) = 768 lines  = 2.88
Ver Blank Start = 12.795; // (msec) = 768 lines  
Ver Blank Time = 0.533; // (msec) = 32 lines  
Ver Sync Start = 12.812; // (msec) = 769 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.017; // (msec) = 1 lines  
Ver Sync Time = 0.050; // (msec) = 3 lines  
// V Back Porch = 0.466; // (msec) = 28 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1024 x 768 @ 85Hz;  
Hor Pixels = 1024; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 68.677; // kHz = 14.6 usec /  line
Ver Frequency = 84.997; // Hz = 11.8 msec /  frame
Pixel Clock = 94.500; // MHz = 10.6 nsec  ± 0.5%
Character Width = 8; // Pixels = 84.7 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.8 %
Hor Sync Polarity = POSITIVE; // HBlank = 25.6% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 5.0% of VTotal  
Hor Total Time = 14.561; // (usec) = 172 chars =  1376 Pixels
Hor Addr Time = 10.836; // (usec) = 128 chars =  1024 Pixels
Hor Blank Start = 10.836; // (usec) = 128 chars =  1024 Pixels
Hor Blank Time = 3.725; // (usec) = 44 chars =  352 Pixels
Hor Sync Start = 11.344; // (usec) = 134 chars =  1072 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.508; // (usec) = 6 chars =  48 Pixels
Hor Sync Time = 1.016; // (usec) = 12 chars =  96 Pixels
// H Back Porch = 2.201; // (usec) = 26 chars =  208 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.765; // (msec) = 808 lines  HT – (1.06xHA)
Ver Addr Time = 11.183; // (msec) = 768 lines  = 3.07
Ver Blank Start = 11.183; // (msec) = 768 lines  
Ver Blank Time = 0.582; // (msec) = 40 lines  
Ver Sync Start = 11.197; // (msec) = 769 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.015; // (msec) = 1 lines  
Ver Sync Time = 0.044; // (msec) = 3 lines  
// V Back Porch = 0.524; // (msec) = 36 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1024 x 768 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1024; // Pixels
Ver Pixels = 768; // Lines
Hor Frequency = 97.551; // kHz = 10.3 usec / line
Ver Frequency = 119.989; // Hz = 8.3 msec / frame
Pixel Clock = 115.500; // MHz = 8.7 nsec ± 0.5%
Character Width = 8; // Pixels = 69.3 nsec
Scan Type = NONINTERLACED; // H Phase = 1.4 %
Hor Sync Polarity = POSITIVE; // HBlank = 13.5% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.5% of VTotal
Hor Total Time = 10.251; // (usec) = 148 chars = 1184 Pixels
Hor Addr Time = 8.866; // (usec) = 128 chars = 1024 Pixels
Hor Blank Start = 8.866; // (usec) = 128 chars = 1024 Pixels
Hor Blank Time = 1.385; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 9.281; // (usec) = 134 chars = 1072 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.416; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.277; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.693; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.334; // (msec) = 813 lines HT – (1.06xHA)
Ver Addr Time = 7.873; // (msec) = 768 lines = 0.85
Ver Blank Start = 7.873; // (msec) = 768 lines
Ver Blank Time = 0.461; // (msec) = 45 lines
Ver Sync Start = 7.904; // (msec) = 771 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.031; // (msec) = 3 lines
Ver Sync Time = 0.041; // (msec) = 4 lines
// V Back Porch = 0.390; // (msec) = 38 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1152 x 864 @ 75Hz;  
Hor Pixels = 1152; // Pixels  
Ver Pixels = 864; // Lines  
Hor Frequency = 67.500; // kHz = 14.8 usec /  line
Ver Frequency = 75.000; // Hz = 13.3 msec /  frame
Pixel Clock = 108.000; // MHz = 9.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 74.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  6.0 %
Hor Sync Polarity = POSITIVE; // HBlank = 28.0% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 14.815; // (usec) = 200 chars =  1600 Pixels
Hor Addr Time = 10.667; // (usec) = 144 chars =  1152 Pixels
Hor Blank Start = 10.667; // (usec) = 144 chars =  1152 Pixels
Hor Blank Time = 4.148; // (usec) = 56 chars =  448 Pixels
Hor Sync Start = 11.259; // (usec) = 152 chars =  1216 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.593; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 1.185; // (usec) = 16 chars =  128 Pixels
// H Back Porch = 2.370; // (usec) = 32 chars =  256 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.333; // (msec) = 900 lines  HT – (1.06xHA)
Ver Addr Time = 12.800; // (msec) = 864 lines  = 3.51
Ver Blank Start = 12.800; // (msec) = 864 lines  
Ver Blank Time = 0.533; // (msec) = 36 lines  
Ver Sync Start = 12.815; // (msec) = 865 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.015; // (msec) = 1 lines  
Ver Sync Time = 0.044; // (msec) = 3 lines  
// V Back Porch = 0.474; // (msec) = 32 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1280 x 720 @ 60Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 720; // Lines  
Hor Frequency = 45.000; // KHz = 22.2 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 74.250; // MHz = 13.5 nsec  ± 0.5%
Character Width = 1; // Pixels = 13.5 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.3 %
Hor Sync Polarity = POSITIVE; // HBlank = 22.4% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 22.222; // (usec) = 1650 chars =  1650 Pixels
Hor Addr Time = 17.239; // (usec) = 1280 chars =  1280 Pixels
Hor Blank Start = 17.239; // (usec) = 1280 chars =  1280 Pixels
Hor Blank Time = 4.983; // (usec) = 370 chars =  370 Pixels
Hor Sync Start = 18.721; // (usec) = 1390 chars =  1390 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 1.481; // (usec) = 110 chars =  110 Pixels
Hor Sync Time = 0.539; // (usec) = 40 chars =  40 Pixels
// H Back Porch = 2.963; // (usec) = 220 chars =  220 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 750 lines  HT – (1.06xHA)
Ver Addr Time = 16.000; // (msec) = 720 lines  = 3.95
Ver Blank Start = 16.000; // (msec) = 720 lines  
Ver Blank Time = 0.667; // (msec) = 30 lines  
Ver Sync Start = 16.111; // (msec) = 725 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.111; // (msec) = 5 lines  
Ver Sync Time = 0.111; // (msec) = 5 lines  
// V Back Porch = 0.444; // (msec) = 20 lines  
  Detailed Timing Parameters
Timing Name = 1280 x 768 @ 60Hz CVT (Reduced Blanking);
Hor Pixels = 1280; // Pixels
Ver Pixels = 768; // Lines
Hor Frequency = 47.396; // kHz = 21.1 usec / line
Ver Frequency = 59.995; // Hz = 16.7 msec / frame
Pixel Clock = 68.250; // MHz = 14.7 nsec ± 0.5%
Character Width = 8; // Pixels = 117.2 nsec
Scan Type = NONINTERLACED; // H Phase = 1.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 11.1% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 2.8% of VTotal
Hor Total Time = 21.099; // (usec) = 180 chars = 1440 Pixels
Hor Addr Time = 18.755; // (usec) = 160 chars = 1280 Pixels
Hor Blank Start = 18.755; // (usec) = 160 chars = 1280 Pixels
Hor Blank Time = 2.344; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 19.458; // (usec) = 166 chars = 1328 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.703; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.469; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 1.172; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 16.668; // (msec) = 790 lines HT – (1.06xHA)
Ver Addr Time = 16.204; // (msec) = 768 lines = 1.22
Ver Blank Start = 16.204; // (msec) = 768 lines
Ver Blank Time = 0.464; // (msec) = 22 lines
Ver Sync Start = 16.267; // (msec) = 771 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.063; // (msec) = 3 lines
Ver Sync Time = 0.148; // (msec) = 7 lines
// V Back Porch = 0.253; // (msec) = 12 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1280 x 768 @ 60Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 47.776; // kHz = 20.9 usec /  line
Ver Frequency = 59.870; // Hz = 16.7 msec /  frame
Pixel Clock = 79.500; // MHz = 12.6 nsec  ± 0.5%
Character Width = 8; // Pixels = 100.6 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = NEGATIVE // HBlank = 23.1% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.8% of VTotal  
Hor Total Time = 20.931; // (usec) = 208 chars =  1664 Pixels
Hor Addr Time = 16.101; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 16.101; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 4.830; // (usec) = 48 chars =  384 Pixels
Hor Sync Start = 16.906; // (usec) = 168 chars =  1344 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.805; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 1.610; // (usec) = 16 chars =  128 Pixels
// H Back Porch = 2.415; // (usec) = 24 chars =  192 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.703; // (msec) = 798 lines  HT – (1.06xHA)
Ver Addr Time = 16.075; // (msec) = 768 lines  = 3.86
Ver Blank Start = 16.075; // (msec) = 768 lines  
Ver Blank Time = 0.628; // (msec) = 30 lines  
Ver Sync Start = 16.138; // (msec) = 771 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.063; // (msec) = 3 lines  
Ver Sync Time = 0.147; // (msec) = 7 lines  
// V Back Porch = 0.419; // (msec) = 20 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1280 x 768 @ 75Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 60.289; // KHz = 16.6 usec /  line
Ver Frequency = 74.893; // Hz = 13.4 msec /  frame
Pixel Clock = 102.250; // MHz = 9.8 nsec  ± 0.5%
Character Width = 8; // Pixels = 78.2 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = NEGATIVE // HBlank = 24.5% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.6% of VTotal  
Hor Total Time = 16.587; // (usec) = 212 chars =  1696 Pixels
Hor Addr Time = 12.518; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 12.518; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 4.068; // (usec) = 52 chars =  416 Pixels
Hor Sync Start = 13.301; // (usec) = 170 chars =  1360 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.782; // (usec) = 10 chars =  80 Pixels
Hor Sync Time = 1.252; // (usec) = 16 chars =  128 Pixels
// H Back Porch = 2.034; // (usec) = 26 chars =  208 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.352; // (msec) = 805 lines  HT – (1.06xHA)
Ver Addr Time = 12.739; // (msec) = 768 lines  = 3.32
Ver Blank Start = 12.739; // (msec) = 768 lines  
Ver Blank Time = 0.614; // (msec) = 37 lines  
Ver Sync Start = 12.788; // (msec) = 771 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.050; // (msec) = 3 lines  
Ver Sync Time = 0.116; // (msec) = 7 lines  
// V Back Porch = 0.448; // (msec) = 27 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1280 x 768 @ 85Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 68.633; // kHz = 14.6 usec /  line
Ver Frequency = 84.837; // Hz = 11.8 msec /  frame
Pixel Clock = 117.500; // MHz = 8.5 nsec  ± 0.5%
Character Width = 8; // Pixels = 68.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE // HBlank = 25.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 5.1% of VTotal  
Hor Total Time = 14.570; // (usec) = 214 chars =  1712 Pixels
Hor Addr Time = 10.894; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 10.894; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 3.677; // (usec) = 54 chars =  432 Pixels
Hor Sync Start = 11.574; // (usec) = 170 chars =  1360 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.681; // (usec) = 10 chars =  80 Pixels
Hor Sync Time = 1.157; // (usec) = 17 chars =  136 Pixels
// H Back Porch = 1.838; // (usec) = 27 chars =  216 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.787; // (msec) = 809 lines  HT – (1.06xHA)
Ver Addr Time = 11.190; // (msec) = 768 lines  = 3.02
Ver Blank Start = 11.190; // (msec) = 768 lines  
Ver Blank Time = 0.597; // (msec) = 41 lines  
Ver Sync Start = 11.234; // (msec) = 771 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.044; // (msec) = 3 lines  
Ver Sync Time = 0.102; // (msec) = 7 lines  
// V Back Porch = 0.452; // (msec) = 31 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1280 x 768 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1280; // Pixels
Ver Pixels = 768; // Lines
Hor Frequency = 97.396; // kHz = 10.3 usec / line
Ver Frequency = 119.798; // Hz = 8.3 msec / frame
Pixel Clock = 140.250; // MHz = 7.1 nsec ± 0.5%
Character Width = 8; // Pixels = 57.0 nsec
Scan Type = NONINTERLACED; // H Phase = 1.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 11.1% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.5% of VTotal
Hor Total Time = 10.267; // (usec) = 180 chars = 1440 Pixels
Hor Addr Time = 9.127; // (usec) = 160 chars = 1280 Pixels
Hor Blank Start = 9.127; // (usec) = 160 chars = 1280 Pixels
Hor Blank Time = 1.141; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 9.469; // (usec) = 166 chars = 1328 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.342; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.228; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.570; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.347; // (msec) = 813 lines HT – (1.06xHA)
Ver Addr Time = 7.885; // (msec) = 768 lines = 0.59
Ver Blank Start = 7.885; // (msec) = 768 lines
Ver Blank Time = 0.462; // (msec) = 45 lines
Ver Sync Start = 7.916; // (msec) = 771 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.031; // (msec) = 3 lines
Ver Sync Time = 0.072; // (msec) = 7 lines
// V Back Porch = 0.359; // (msec) = 35 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters
Timing Name = 1280 x 800 @ 60Hz CVT (Reduced Blanking);
Hor Pixels = 1280; // Pixels
Ver Pixels = 800; // Lines
Hor Frequency = 49.306; // kHz = 20.3 usec / line
Ver Frequency = 59.910; // Hz = 16.7 msec / frame
Pixel Clock = 71.000; // MHz = 14.1 nsec ± 0.5%
Character Width = 8; // Pixels = 112.7 nsec
Scan Type = NONINTERLACED; // H Phase = 1.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 11.1% of HTotal
Ver Sync Polarity = NEGATIVE; // VBlank = 2.8% of VTotal
Hor Total Time = 20.282; // (usec) = 180 chars = 1440 Pixels
Hor Addr Time = 18.028; // (usec) = 160 chars = 1280 Pixels
Hor Blank Start = 18.028; // (usec) = 160 chars = 1280 Pixels
Hor Blank Time = 2.254; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 18.704; // (usec) = 166 chars = 1328 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.676; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.451; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 1.127; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 16.692; // (msec) = 823 lines HT – (1.06xHA)
Ver Addr Time = 16.225; // (msec) = 800 lines = 1.17
Ver Blank Start = 16.225; // (msec) = 800 lines
Ver Blank Time = 0.466; // (msec) = 23 lines
Ver Sync Start = 16.286; // (msec) = 803 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.061; // (msec) = 3 lines
Ver Sync Time = 0.122; // (msec) = 6 lines
// V Back Porch = 0.284; // (msec) = 14 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1280 x 800 @ 60Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 800; // Lines  
Hor Frequency = 49.702; // kHz = 20.1 usec /  line
Ver Frequency = 59.810; // Hz = 16.7 msec /  frame
Pixel Clock = 83.500; // MHz = 12.0 nsec  ± 0.5%
Character Width = 8; // Pixels = 95.8 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = NEGATIVE; // HBlank = 23.8% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.7% of VTotal  
Hor Total Time = 20.120; // (usec) = 210 chars =  1680 Pixels
Hor Addr Time = 15.329; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 15.329; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 4.790; // (usec) = 50 chars =  400 Pixels
Hor Sync Start = 16.192; // (usec) = 169 chars =  1352 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.862; // (usec) = 9 chars =  72 Pixels
Hor Sync Time = 1.533; // (usec) = 16 chars =  128 Pixels
// H Back Porch = 2.395; // (usec) = 25 chars =  200 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.720; // (msec) = 831 lines  HT – (1.06xHA)
Ver Addr Time = 16.096; // (msec) = 800 lines  = 3.87
Ver Blank Start = 16.096; // (msec) = 800 lines  
Ver Blank Time = 0.624; // (msec) = 31 lines  
Ver Sync Start = 16.156; // (msec) = 803 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.060; // (msec) = 3 lines  
Ver Sync Time = 0.121; // (msec) = 6 lines  
// V Back Porch = 0.443; // (msec) = 22 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1280 x 800 @ 75Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 800; // Lines  
Hor Frequency = 62.795; // kHz = 15.9 usec /  line
Ver Frequency = 74.934; // Hz = 13.3 msec /  frame
Pixel Clock = 106.500; // MHz = 9.4 nsec  ± 0.5%
Character Width = 8; // Pixels = 75.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = NEGATIVE; // HBlank = 24.5% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.5% of VTotal  
Hor Total Time = 15.925; // (usec) = 212 chars =  1696 Pixels
Hor Addr Time = 12.019; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 12.019; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 3.906; // (usec) = 52 chars =  416 Pixels
Hor Sync Start = 12.770; // (usec) = 170 chars =  1360 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.751; // (usec) = 10 chars =  80 Pixels
Hor Sync Time = 1.202; // (usec) = 16 chars =  128 Pixels
// H Back Porch = 1.953; // (usec) = 26 chars =  208 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.345; // (msec) = 838 lines  HT – (1.06xHA)
Ver Addr Time = 12.740; // (msec) = 800 lines  = 3.18
Ver Blank Start = 12.740; // (msec) = 800 lines  
Ver Blank Time = 0.605; // (msec) = 38 lines  
Ver Sync Start = 12.788; // (msec) = 803 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.048; // (msec) = 3 lines  
Ver Sync Time = 0.096; // (msec) = 6 lines  
// V Back Porch = 0.462; // (msec) = 29 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1280 x 800 @ 85Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 800; // Lines  
Hor Frequency = 71.554; // kHz = 14.0 usec /  line
Ver Frequency = 84.880; // Hz = 11.8 msec /  frame
Pixel Clock = 122.500; // MHz = 8.2 nsec  ± 0.5%
Character Width = 8; // Pixels = 65.3 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE; // HBlank = 25.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 5.1% of VTotal  
Hor Total Time = 13.976; // (usec) = 214 chars =  1712 Pixels
Hor Addr Time = 10.449; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 10.449; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 3.527; // (usec) = 54 chars =  432 Pixels
Hor Sync Start = 11.102; // (usec) = 170 chars =  1360 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.653; // (usec) = 10 chars =  80 Pixels
Hor Sync Time = 1.110; // (usec) = 17 chars =  136 Pixels
// H Back Porch = 1.763; // (usec) = 27 chars =  216 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.781; // (msec) = 843 lines  HT – (1.06xHA)
Ver Addr Time = 11.180; // (msec) = 800 lines  = 2.9
Ver Blank Start = 11.180; // (msec) = 800 lines  
Ver Blank Time = 0.601; // (msec) = 43 lines  
Ver Sync Start = 11.222; // (msec) = 803 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.042; // (msec) = 3 lines  
Ver Sync Time = 0.084; // (msec) = 6 lines  
// V Back Porch = 0.475; // (msec) = 34 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1280 x 800 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1280; // Pixels
Ver Pixels = 800; // Lines
Hor Frequency = 101.563; // kHz = 9.8 usec / line
Ver Frequency = 119.909; // Hz = 8.3 msec / frame
Pixel Clock = 146.250; // MHz = 6.8 nsec ± 0.5%
Character Width = 8; // Pixels = 54.7 nsec
Scan Type = NONINTERLACED; // H Phase = 1.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 11.1% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.5% of VTotal
Hor Total Time = 9.846; // (usec) = 180 chars = 1440 Pixels
Hor Addr Time = 8.752; // (usec) = 160 chars = 1280 Pixels
Hor Blank Start = 8.752; // (usec) = 160 chars = 1280 Pixels
Hor Blank Time = 1.094; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 9.080; // (usec) = 166 chars = 1328 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.328; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.219; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.547; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.340; // (msec) = 847 lines HT – (1.06xHA)
Ver Addr Time = 7.877; // (msec) = 800 lines = 0.57
Ver Blank Start = 7.877; // (msec) = 800 lines
Ver Blank Time = 0.463; // (msec) = 47 lines
Ver Sync Start = 7.906; // (msec) = 803 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.030; // (msec) = 3 lines
Ver Sync Time = 0.059; // (msec) = 6 lines
// V Back Porch = 0.374; // (msec) = 38 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1280 x 960 @ 60Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 960; // Lines  
Hor Frequency = 60.000; // kHz = 16.7 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 108.000; // MHz = 9.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 74.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  6.0 %
Hor Sync Polarity = POSITIVE; // HBlank = 28.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 16.667; // (usec) = 225 chars =  1800 Pixels
Hor Addr Time = 11.852; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 11.852; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 4.815; // (usec) = 65 chars =  520 Pixels
Hor Sync Start = 12.741; // (usec) = 172 chars =  1376 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.889; // (usec) = 12 chars =  96 Pixels
Hor Sync Time = 1.037; // (usec) = 14 chars =  112 Pixels
// H Back Porch = 2.889; // (usec) = 39 chars =  312 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 1000 lines  HT – (1.06xHA)
Ver Addr Time = 16.000; // (msec) = 960 lines  = 4.1
Ver Blank Start = 16.000; // (msec) = 960 lines  
Ver Blank Time = 0.667; // (msec) = 40 lines  
Ver Sync Start = 16.017; // (msec) = 961 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.017; // (msec) = 1 lines  
Ver Sync Time = 0.050; // (msec) = 3 lines  
// V Back Porch = 0.600; // (msec) = 36 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1280 x 960 @ 85Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 960; // Lines  
Hor Frequency = 85.938; // kHz = 11.6 usec /  line
Ver Frequency = 85.002; // Hz = 11.8 msec /  frame
Pixel Clock = 148.500; // MHz = 6.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 53.9 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 25.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 5.0% of VTotal  
Hor Total Time = 11.636; // (usec) = 216 chars =  1728 Pixels
Hor Addr Time = 8.620; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 8.620; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 3.017; // (usec) = 56 chars =  448 Pixels
Hor Sync Start = 9.051; // (usec) = 168 chars =  1344 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.431; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 1.077; // (usec) = 20 chars =  160 Pixels
// H Back Porch = 1.508; // (usec) = 28 chars =  224 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.764; // (msec) = 1011 lines  HT – (1.06xHA)
Ver Addr Time = 11.171; // (msec) = 960 lines  = 2.5
Ver Blank Start = 11.171; // (msec) = 960 lines  
Ver Blank Time = 0.593; // (msec) = 51 lines  
Ver Sync Start = 11.183; // (msec) = 961 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.012; // (msec) = 1 lines  
Ver Sync Time = 0.035; // (msec) = 3 lines  
// V Back Porch = 0.547; // (msec) = 47 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1280 x 960 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1280; // Pixels
Ver Pixels = 960; // Lines
Hor Frequency = 121.875; // kHz = 8.2 usec / line
Ver Frequency = 119.838; // Hz = 8.3 msec / frame
Pixel Clock = 175.500; // MHz = 5.7 nsec ± 0.5%
Character Width = 8; // Pixels = 45.6 nsec
Scan Type = NONINTERLACED; // H Phase = 1.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 11.1% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 8.205; // (usec) = 180 chars = 1440 Pixels
Hor Addr Time = 7.293; // (usec) = 160 chars = 1280 Pixels
Hor Blank Start = 7.293; // (usec) = 160 chars = 1280 Pixels
Hor Blank Time = 0.912; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 7.567; // (usec) = 166 chars = 1328 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.274; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.182; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.456; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.345; // (msec) = 1017 lines HT – (1.06xHA)
Ver Addr Time = 7.877; // (msec) = 960 lines = 0.47
Ver Blank Start = 7.877; // (msec) = 960 lines
Ver Blank Time = 0.468; // (msec) = 57 lines
Ver Sync Start = 7.902; // (msec) = 963 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.025; // (msec) = 3 lines
Ver Sync Time = 0.033; // (msec) = 4 lines
// V Back Porch = 0.410; // (msec) = 50 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1280 x 1024 @ 60Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 1024; // Lines  
Hor Frequency = 63.981; // kHz = 15.6 usec /  line
Ver Frequency = 60.020; // Hz = 16.7 msec /  frame
Pixel Clock = 108.000; // MHz = 9.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 74.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.9 %
Hor Sync Polarity = POSITIVE; // HBlank = 24.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.9% of VTotal  
Hor Total Time = 15.630; // (usec) = 211 chars =  1688 Pixels
Hor Addr Time = 11.852; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 11.852; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 3.778; // (usec) = 51 chars =  408 Pixels
Hor Sync Start = 12.296; // (usec) = 166 chars =  1328 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.444; // (usec) = 6 chars =  48 Pixels
Hor Sync Time = 1.037; // (usec) = 14 chars =  112 Pixels
// H Back Porch = 2.296; // (usec) = 31 chars =  248 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.661; // (msec) = 1066 lines  HT – (1.06xHA)
Ver Addr Time = 16.005; // (msec) = 1024 lines  = 3.07
Ver Blank Start = 16.005; // (msec) = 1024 lines  
Ver Blank Time = 0.656; // (msec) = 42 lines  
Ver Sync Start = 16.020; // (msec) = 1025 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.016; // (msec) = 1 lines  
Ver Sync Time = 0.047; // (msec) = 3 lines  
// V Back Porch = 0.594; // (msec) = 38 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1280 x 1024 @ 75Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 1024; // Lines  
Hor Frequency = 79.976; // kHz = 12.5 usec /  line
Ver Frequency = 75.025; // Hz = 13.3 msec /  frame
Pixel Clock = 135.000; // MHz = 7.4 nsec  ± 0.5%
Character Width = 8; // Pixels = 59.3 nsec  
Scan Type = NONINTERLACED; // H Phase =  6.9 %
Hor Sync Polarity = POSITIVE; // HBlank = 24.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.9% of VTotal  
Hor Total Time = 12.504; // (usec) = 211 chars =  1688 Pixels
Hor Addr Time = 9.481; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 9.481; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 3.022; // (usec) = 51 chars =  408 Pixels
Hor Sync Start = 9.600; // (usec) = 162 chars =  1296 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.119; // (usec) = 2 chars =  16 Pixels
Hor Sync Time = 1.067; // (usec) = 18 chars =  144 Pixels
// H Back Porch = 1.837; // (usec) = 31 chars =  248 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.329; // (msec) = 1066 lines  HT – (1.06xHA)
Ver Addr Time = 12.804; // (msec) = 1024 lines  = 2.45
Ver Blank Start = 12.804; // (msec) = 1024 lines  
Ver Blank Time = 0.525; // (msec) = 42 lines  
Ver Sync Start = 12.816; // (msec) = 1025 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.013; // (msec) = 1 lines  
Ver Sync Time = 0.038; // (msec) = 3 lines  
// V Back Porch = 0.475; // (msec) = 38 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1280 x 1024 @ 85Hz;  
Hor Pixels = 1280; // Pixels  
Ver Pixels = 1024; // Lines  
Hor Frequency = 91.146; // kHz = 11.0 usec /  line
Ver Frequency = 85.024; // Hz = 11.8 msec /  frame
Pixel Clock = 157.500; // MHz = 6.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 50.8 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 25.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.5% of VTotal  
Hor Total Time = 10.971; // (usec) = 216 chars =  1728 Pixels
Hor Addr Time = 8.127; // (usec) = 160 chars =  1280 Pixels
Hor Blank Start = 8.127; // (usec) = 160 chars =  1280 Pixels
Hor Blank Time = 2.844; // (usec) = 56 chars =  448 Pixels
Hor Sync Start = 8.533; // (usec) = 168 chars =  1344 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.406; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 1.016; // (usec) = 20 chars =  160 Pixels
// H Back Porch = 1.422; // (usec) = 28 chars =  224 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.761; // (msec) = 1072 lines  HT – (1.06xHA)
Ver Addr Time = 11.235; // (msec) = 1024 lines  = 2.36
Ver Blank Start = 11.235; // (msec) = 1024 lines  
Ver Blank Time = 0.527; // (msec) = 48 lines  
Ver Sync Start = 11.246; // (msec) = 1025 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.011; // (msec) = 1 lines  
Ver Sync Time = 0.033; // (msec) = 3 lines  
// V Back Porch = 0.483; // (msec) = 44 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1280 x 1024 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1280; // Pixels
Ver Pixels = 1024; // Lines
Hor Frequency = 130.035; // kHz = 7.7 usec / line
Ver Frequency = 119.958; // Hz = 8.3 msec / frame
Pixel Clock = 187.250; // MHz = 5.3 nsec ± 0.5%
Character Width = 8; // Pixels = 42.7 nsec
Scan Type = NONINTERLACED; // H Phase = 1.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 11.1% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.5% of VTotal
Hor Total Time = 7.690; // (usec) = 180 chars = 1440 Pixels
Hor Addr Time = 6.836; // (usec) = 160 chars = 1280 Pixels
Hor Blank Start = 6.836; // (usec) = 160 chars = 1280 Pixels
Hor Blank Time = 0.854; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 7.092; // (usec) = 166 chars = 1328 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.256; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.171; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.427; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.336; // (msec) = 1084 lines HT – (1.06xHA)
Ver Addr Time = 7.875; // (msec) = 1024 lines = 0.44
Ver Blank Start = 7.875; // (msec) = 1024 lines
Ver Blank Time = 0.461; // (msec) = 60 lines
Ver Sync Start = 7.898; // (msec) = 1027 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.023; // (msec) = 3 lines
Ver Sync Time = 0.054; // (msec) = 7 lines
// V Back Porch = 0.385; // (msec) = 50 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1360 x 768 @ 60Hz;  
Hor Pixels = 1360; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 47.712; // kHz = 21.0 usec /  line
Ver Frequency = 60.015; // Hz = 16.7 msec /  frame
Pixel Clock = 85.500; // MHz = 11.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 93.6 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.4 %
Hor Sync Polarity = POSITIVE; // HBlank = 24.1% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.4% of VTotal  
Hor Total Time = 20.959; // (usec) = 224 chars =  1792 Pixels
Hor Addr Time = 15.906; // (usec) = 170 chars =  1360 Pixels
Hor Blank Start = 15.906; // (usec) = 170 chars =  1360 Pixels
Hor Blank Time = 5.053; // (usec) = 54 chars =  432 Pixels
Hor Sync Start = 16.655; // (usec) = 178 chars =  1424 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.749; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 1.310; // (usec) = 14 chars =  112 Pixels
// H Back Porch = 2.994; // (usec) = 32 chars =  256 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.662; // (msec) = 795 lines  HT – (1.06xHA)
Ver Addr Time = 16.097; // (msec) = 768 lines  = 4.1
Ver Blank Start = 16.097; // (msec) = 768 lines  
Ver Blank Time = 0.566; // (msec) = 27 lines  
Ver Sync Start = 16.159; // (msec) = 771 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.063; // (msec) = 3 lines  
Ver Sync Time = 0.126; // (msec) = 6 lines  
// V Back Porch = 0.377; // (msec) = 18 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1360 x 768 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1360; // Pixels
Ver Pixels = 768; // Lines
Hor Frequency = 97.533; // kHz = 10.3 usec / line
Ver Frequency = 119.967; // Hz = 8.3 msec / frame
Pixel Clock = 148.250; // MHz = 6.7 nsec ± 0.5%
Character Width = 8; // Pixels = 54.0 nsec
Scan Type = NONINTERLACED; // H Phase = 1.1 %
Hor Sync Polarity = POSITIVE; // HBlank = 10.5% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.5% of VTotal
Hor Total Time = 10.253; // (usec) = 190 chars = 1520 Pixels
Hor Addr Time = 9.174; // (usec) = 170 chars = 1360 Pixels
Hor Blank Start = 9.174; // (usec) = 170 chars = 1360 Pixels
Hor Blank Time = 1.079; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 9.497; // (usec) = 176 chars = 1408 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.324; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.216; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.540; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.336; // (msec) = 813 lines HT – (1.06xHA)
Ver Addr Time = 7.874; // (msec) = 768 lines = 0.53
Ver Blank Start = 7.874; // (msec) = 768 lines
Ver Blank Time = 0.461; // (msec) = 45 lines
Ver Sync Start = 7.905; // (msec) = 771 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.031; // (msec) = 3 lines
Ver Sync Time = 0.051; // (msec) = 5 lines
// V Back Porch = 0.379; // (msec) = 37 lines
// V Top Border = 0.000; // (msec) = 0 lines
    
  Detailed Timing Parameters  
Timing Name = 1366 x 768 @ 60Hz;  
Hor Pixels = 1366; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 47.712; // KHz = 21.0 usec /  line
Ver Frequency = 59.790; // Hz = 16.7 msec /  frame
Pixel Clock = 85.500; // MHz = 11.7 nsec  ± 0.5%
Character Width = 1; // Pixels = 11.7 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = POSITIVE; // HBlank = 23.8% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.8% of VTotal  
Hor Total Time = 20.959; // (usec) = 1792 chars =  1792 Pixels
Hor Addr Time = 15.977; // (usec) = 1366 chars =  1366 Pixels
Hor Blank Start = 15.977; // (usec) = 1366 chars =  1366 Pixels
Hor Blank Time = 4.982; // (usec) = 426 chars =  426 Pixels
Hor Sync Start = 16.795; // (usec) = 1436 chars =  1436 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.819; // (usec) = 70 chars =  70 Pixels
Hor Sync Time = 1.673; // (usec) = 143 chars =  143 Pixels
// H Back Porch = 2.491; // (usec) = 213 chars =  213 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.725; // (msec) = 798 lines  HT – (1.06xHA)
Ver Addr Time = 16.097; // (msec) = 768 lines  = 4.02
Ver Blank Start = 16.097; // (msec) = 768 lines  
Ver Blank Time = 0.629; // (msec) = 30 lines  
Ver Sync Start = 16.159; // (msec) = 771 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.063; // (msec) = 3 lines  
Ver Sync Time = 0.063; // (msec) = 3 lines  
// V Back Porch = 0.503; // (msec) = 24 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1366 x 768 @ 60Hz;  
Hor Pixels = 1366; // Pixels  
Ver Pixels = 768; // Lines  
Hor Frequency = 48.000; // KHz = 20.8 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 72.000; // MHz = 13.9 nsec  ± 0.5%
Character Width = 1; // Pixels = 13.9 nsec  
Scan Type = NONINTERLACED; // H Phase =  1.7 %
Hor Sync Polarity = POSITIVE; // HBlank = 8.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 20.833; // (usec) = 1500 chars =  1500 Pixels
Hor Addr Time = 18.972; // (usec) = 1366 chars =  1366 Pixels
Hor Blank Start = 18.972; // (usec) = 1366 chars =  1366 Pixels
Hor Blank Time = 1.861; // (usec) = 134 chars =  134 Pixels
Hor Sync Start = 19.167; // (usec) = 1380 chars =  1380 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.194; // (usec) = 14 chars =  14 Pixels
Hor Sync Time = 0.778; // (usec) = 56 chars =  56 Pixels
// H Back Porch = 0.889; // (usec) = 64 chars =  64 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 800 lines  HT – (1.06xHA)
Ver Addr Time = 16.000; // (msec) = 768 lines  = 0.72
Ver Blank Start = 16.000; // (msec) = 768 lines  
Ver Blank Time = 0.667; // (msec) = 32 lines  
Ver Sync Start = 16.021; // (msec) = 769 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.021; // (msec) = 1 lines  
Ver Sync Time = 0.063; // (msec) = 3 lines  
// V Back Porch = 0.583; // (msec) = 28 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1400 x 1050 @ 60Hz CVT (Reduced Blanking);
Hor Pixels = 1400; // Pixels
Ver Pixels = 1050; // Lines
Hor Frequency = 64.744; // kHz = 15.4 usec / line
Ver Frequency = 59.948; // Hz = 16.7 msec / frame
Pixel Clock = 101.000; // MHz = 9.9 nsec ± 0.5%
Character Width = 8; // Pixels = 79.2 nsec
Scan Type = NONINTERLACED; // H Phase = 1.0 %
Hor Sync Polarity = POSITIVE; // HBlank = 10.3% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 2.8% of VTotal
Hor Total Time = 15.446; // (usec) = 195 chars = 1560 Pixels
Hor Addr Time = 13.861; // (usec) = 175 chars = 1400 Pixels
Hor Blank Start = 13.861; // (usec) = 175 chars = 1400 Pixels
Hor Blank Time = 1.584; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 14.337; // (usec) = 181 chars = 1448 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.475; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.317; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.792; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 16.681; // (msec) = 1080 lines HT – (1.06xHA)
Ver Addr Time = 16.218; // (msec) = 1050 lines = 0.75
Ver Blank Start = 16.218; // (msec) = 1050 lines
Ver Blank Time = 0.463; // (msec) = 30 lines
Ver Sync Start = 16.264; // (msec) = 1053 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.046; // (msec) = 3 lines
Ver Sync Time = 0.062; // (msec) = 4 lines
// V Back Porch = 0.355; // (msec) = 23 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1400 x 1050 @ 60Hz;;  
Hor Pixels = 1400; // Pixels  
Ver Pixels = 1050; // Lines  
Hor Frequency = 65.317; // kHz = 15.3 usec /  line
Ver Frequency = 59.978; // Hz = 16.7 msec /  frame
Pixel Clock = 121.750; // MHz = 8.2 nsec  ± 0.5%
Character Width = 8; // Pixels = 65.7 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.9 %
Hor Sync Polarity = NEGATIVE // HBlank = 24.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.6% of VTotal  
Hor Total Time = 15.310; // (usec) = 233 chars =  1864 Pixels
Hor Addr Time = 11.499; // (usec) = 175 chars =  1400 Pixels
Hor Blank Start = 11.499; // (usec) = 175 chars =  1400 Pixels
Hor Blank Time = 3.811; // (usec) = 58 chars =  464 Pixels
Hor Sync Start = 12.222; // (usec) = 186 chars =  1488 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.723; // (usec) = 11 chars =  88 Pixels
Hor Sync Time = 1.183; // (usec) = 18 chars =  144 Pixels
// H Back Porch = 1.906; // (usec) = 29 chars =  232 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.673; // (msec) = 1089 lines  HT – (1.06xHA)
Ver Addr Time = 16.076; // (msec) = 1050 lines  = 3.12
Ver Blank Start = 16.076; // (msec) = 1050 lines  
Ver Blank Time = 0.597; // (msec) = 39 lines  
Ver Sync Start = 16.121; // (msec) = 1053 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.046; // (msec) = 3 lines  
Ver Sync Time = 0.061; // (msec) = 4 lines  
// V Back Porch = 0.490; // (msec) = 32 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1400 x 1050 @ 75Hz;  
Hor Pixels = 1400; // Pixels  
Ver Pixels = 1050; // Lines  
Hor Frequency = 82.278; // kHz = 12.2 usec /  line
Ver Frequency = 74.867; // Hz = 13.4 msec /  frame
Pixel Clock = 156.000; // MHz = 6.4 nsec  ± 0.5%
Character Width = 8; // Pixels = 51.3 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = NEGATIVE // HBlank = 26.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.5% of VTotal  
Hor Total Time = 12.154; // (usec) = 237 chars =  1896 Pixels
Hor Addr Time = 8.974; // (usec) = 175 chars =  1400 Pixels
Hor Blank Start = 8.974; // (usec) = 175 chars =  1400 Pixels
Hor Blank Time = 3.179; // (usec) = 62 chars =  496 Pixels
Hor Sync Start = 9.641; // (usec) = 188 chars =  1504 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.667; // (usec) = 13 chars =  104 Pixels
Hor Sync Time = 0.923; // (usec) = 18 chars =  144 Pixels
// H Back Porch = 1.590; // (usec) = 31 chars =  248 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.357; // (msec) = 1099 lines  HT – (1.06xHA)
Ver Addr Time = 12.762; // (msec) = 1050 lines  = 2.64
Ver Blank Start = 12.762; // (msec) = 1050 lines  
Ver Blank Time = 0.596; // (msec) = 49 lines  
Ver Sync Start = 12.798; // (msec) = 1053 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.036; // (msec) = 3 lines  
Ver Sync Time = 0.049; // (msec) = 4 lines  
// V Back Porch = 0.510; // (msec) = 42 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1400 x 1050 @ 85Hz;  
Hor Pixels = 1400; // Pixels  
Ver Pixels = 1050; // Lines  
Hor Frequency = 93.881; // kHz = 10.7 usec /  line
Ver Frequency = 84.960; // Hz = 11.8 msec /  frame
Pixel Clock = 179.500; // MHz = 5.6 nsec  ± 0.5%
Character Width = 8; // Pixels = 44.6 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE // HBlank = 26.8% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 5.0% of VTotal  
Hor Total Time = 10.652; // (usec) = 239 chars =  1912 Pixels
Hor Addr Time = 7.799; // (usec) = 175 chars =  1400 Pixels
Hor Blank Start = 7.799; // (usec) = 175 chars =  1400 Pixels
Hor Blank Time = 2.852; // (usec) = 64 chars =  512 Pixels
Hor Sync Start = 8.379; // (usec) = 188 chars =  1504 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.579; // (usec) = 13 chars =  104 Pixels
Hor Sync Time = 0.847; // (usec) = 19 chars =  152 Pixels
// H Back Porch = 1.426; // (usec) = 32 chars =  256 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.770; // (msec) = 1105 lines  HT – (1.06xHA)
Ver Addr Time = 11.184; // (msec) = 1050 lines  = 2.38
Ver Blank Start = 11.184; // (msec) = 1050 lines  
Ver Blank Time = 0.586; // (msec) = 55 lines  
Ver Sync Start = 11.216; // (msec) = 1053 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.032; // (msec) = 3 lines  
Ver Sync Time = 0.043; // (msec) = 4 lines  
// V Back Porch = 0.511; // (msec) = 48 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1400 x 1050 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1400; // Pixels
Ver Pixels = 1050; // Lines
Hor Frequency = 133.333; // kHz = 7.5 usec / line
Ver Frequency = 119.904; // Hz = 8.3 msec / frame
Pixel Clock = 208.000; // MHz = 4.8 nsec ± 0.5%
Character Width = 8; // Pixels = 38.5 nsec
Scan Type = NONINTERLACED; // H Phase = 1.0 %
Hor Sync Polarity = POSITIVE; // HBlank = 10.3% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 7.500; // (usec) = 195 chars = 1560 Pixels
Hor Addr Time = 6.731; // (usec) = 175 chars = 1400 Pixels
Hor Blank Start = 6.731; // (usec) = 175 chars = 1400 Pixels
Hor Blank Time = 0.769; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 6.962; // (usec) = 181 chars = 1448 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.231; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.154; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.385; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.340; // (msec) = 1112 lines HT – (1.06xHA)
Ver Addr Time = 7.875; // (msec) = 1050 lines = 0.37
Ver Blank Start = 7.875; // (msec) = 1050 lines
Ver Blank Time = 0.465; // (msec) = 62 lines
Ver Sync Start = 7.898; // (msec) = 1053 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.023; // (msec) = 3 lines
Ver Sync Time = 0.030; // (msec) = 4 lines
// V Back Porch = 0.413; // (msec) = 55 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters
Timing Name = 1440 x 900 @ 60Hz CVT (Reduced Blanking);
Hor Pixels = 1440; // Pixels
Ver Pixels = 900; // Lines
Hor Frequency = 55.469; // kHz = 18.0 usec / line
Ver Frequency = 59.901; // Hz = 16.7 msec / frame
Pixel Clock = 88.750; // MHz = 11.3 nsec ± 0.5%
Character Width = 8; // Pixels = 90.1 nsec
Scan Type = NONINTERLACED; // H Phase = 1.0 %
Hor Sync Polarity = POSITIVE; // HBlank = 10.0% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 2.8% of VTotal
Hor Total Time = 18.028; // (usec) = 200 chars = 1600 Pixels
Hor Addr Time = 16.225; // (usec) = 180 chars = 1440 Pixels
Hor Blank Start = 16.225; // (usec) = 180 chars = 1440 Pixels
Hor Blank Time = 1.803; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 16.766; // (usec) = 186 chars = 1488 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.541; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.361; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.901; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 16.694; // (msec) = 926 lines HT – (1.06xHA)
Ver Addr Time = 16.225; // (msec) = 900 lines = 0.83
Ver Blank Start = 16.225; // (msec) = 900 lines
Ver Blank Time = 0.469; // (msec) = 26 lines
Ver Sync Start = 16.279; // (msec) = 903 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.054; // (msec) = 3 lines
Ver Sync Time = 0.108; // (msec) = 6 lines
// V Back Porch = 0.306; // (msec) = 17 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1440 x 900 @ 60Hz;  
Hor Pixels = 1440; // Pixels  
Ver Pixels = 900; // Lines  
Hor Frequency = 55.935; // kHz = 17.9 usec /  line
Ver Frequency = 59.887; // Hz = 16.7 msec /  frame
Pixel Clock = 106.500; // MHz = 9.4 nsec  ± 0.5%
Character Width = 8; // Pixels = 75.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE // HBlank = 24.4% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.6% of VTotal  
Hor Total Time = 17.878; // (usec) = 238 chars =  1904 Pixels
Hor Addr Time = 13.521; // (usec) = 180 chars =  1440 Pixels
Hor Blank Start = 13.521; // (usec) = 180 chars =  1440 Pixels
Hor Blank Time = 4.357; // (usec) = 58 chars =  464 Pixels
Hor Sync Start = 14.272; // (usec) = 190 chars =  1520 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.751; // (usec) = 10 chars =  80 Pixels
Hor Sync Time = 1.427; // (usec) = 19 chars =  152 Pixels
// H Back Porch = 2.178; // (usec) = 29 chars =  232 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.698; // (msec) = 934 lines  HT – (1.06xHA)
Ver Addr Time = 16.090; // (msec) = 900 lines  = 3.55
Ver Blank Start = 16.090; // (msec) = 900 lines  
Ver Blank Time = 0.608; // (msec) = 34 lines  
Ver Sync Start = 16.144; // (msec) = 903 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.054; // (msec) = 3 lines  
Ver Sync Time = 0.107; // (msec) = 6 lines  
// V Back Porch = 0.447; // (msec) = 25 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1440 x 900 @ 75Hz;  
Hor Pixels = 1440; // Pixels  
Ver Pixels = 900; // Lines  
Hor Frequency = 70.635; // kHz = 14.2 usec /  line
Ver Frequency = 74.984; // Hz = 13.3 msec /  frame
Pixel Clock = 136.750; // MHz = 7.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 58.5 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.9 %
Hor Sync Polarity = NEGATIVE // HBlank = 25.6% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.5% of VTotal  
Hor Total Time = 14.157; // (usec) = 242 chars =  1936 Pixels
Hor Addr Time = 10.530; // (usec) = 180 chars =  1440 Pixels
Hor Blank Start = 10.530; // (usec) = 180 chars =  1440 Pixels
Hor Blank Time = 3.627; // (usec) = 62 chars =  496 Pixels
Hor Sync Start = 11.232; // (usec) = 192 chars =  1536 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.702; // (usec) = 12 chars =  96 Pixels
Hor Sync Time = 1.112; // (usec) = 19 chars =  152 Pixels
// H Back Porch = 1.814; // (usec) = 31 chars =  248 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.336; // (msec) = 942 lines  HT – (1.06xHA)
Ver Addr Time = 12.741; // (msec) = 900 lines  = 3
Ver Blank Start = 12.741; // (msec) = 900 lines  
Ver Blank Time = 0.595; // (msec) = 42 lines  
Ver Sync Start = 12.784; // (msec) = 903 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.042; // (msec) = 3 lines  
Ver Sync Time = 0.085; // (msec) = 6 lines  
// V Back Porch = 0.467; // (msec) = 33 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1440 x 900 @ 85Hz;  
Hor Pixels = 1440; // Pixels  
Ver Pixels = 900; // Lines  
Hor Frequency = 80.430; // kHz = 12.4 usec /  line
Ver Frequency = 84.842; // Hz = 11.8 msec /  frame
Pixel Clock = 157.000; // MHz = 6.4 nsec  ± 0.5%
Character Width = 8; // Pixels = 51.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.9 %
Hor Sync Polarity = NEGATIVE // HBlank = 26.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 5.1% of VTotal  
Hor Total Time = 12.433; // (usec) = 244 chars =  1952 Pixels
Hor Addr Time = 9.172; // (usec) = 180 chars =  1440 Pixels
Hor Blank Start = 9.172; // (usec) = 180 chars =  1440 Pixels
Hor Blank Time = 3.261; // (usec) = 64 chars =  512 Pixels
Hor Sync Start = 9.834; // (usec) = 193 chars =  1544 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.662; // (usec) = 13 chars =  104 Pixels
Hor Sync Time = 0.968; // (usec) = 19 chars =  152 Pixels
// H Back Porch = 1.631; // (usec) = 32 chars =  256 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.787; // (msec) = 948 lines  HT – (1.06xHA)
Ver Addr Time = 11.190; // (msec) = 900 lines  = 2.71
Ver Blank Start = 11.190; // (msec) = 900 lines  
Ver Blank Time = 0.597; // (msec) = 48 lines  
Ver Sync Start = 11.227; // (msec) = 903 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.037; // (msec) = 3 lines  
Ver Sync Time = 0.075; // (msec) = 6 lines  
// V Back Porch = 0.485; // (msec) = 39 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1440 x 900 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1440; // Pixels
Ver Pixels = 900; // Lines
Hor Frequency = 114.219; // kHz = 8.8 usec / line
Ver Frequency = 119.852; // Hz = 8.3 msec / frame
Pixel Clock = 182.750; // MHz = 5.5 nsec ± 0.5%
Character Width = 8; // Pixels = 43.8 nsec
Scan Type = NONINTERLACED; // H Phase = 1.0 %
Hor Sync Polarity = POSITIVE; // HBlank = 10.0% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 8.755; // (usec) = 200 chars = 1600 Pixels
Hor Addr Time = 7.880; // (usec) = 180 chars = 1440 Pixels
Hor Blank Start = 7.880; // (usec) = 180 chars = 1440 Pixels
Hor Blank Time = 0.876; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 8.142; // (usec) = 186 chars = 1488 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.263; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.175; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.438; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.344; // (msec) = 953 lines HT – (1.06xHA)
Ver Addr Time = 7.880; // (msec) = 900 lines = 0.4
Ver Blank Start = 7.880; // (msec) = 900 lines
Ver Blank Time = 0.464; // (msec) = 53 lines
Ver Sync Start = 7.906; // (msec) = 903 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.026; // (msec) = 3 lines
Ver Sync Time = 0.053; // (msec) = 6 lines
// V Back Porch = 0.385; // (msec) = 44 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1600 x 900 @ 60Hz;  
Hor Pixels = 1600; // Pixels  
Ver Pixels = 900; // Lines  
Hor Frequency = 60.000; // KHz = 16.7 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 108.000; // MHz = 9.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 74.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  2.0 %
Hor Sync Polarity = POSITIVE; // HBlank = 11.1% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 10.0% of VTotal  
Hor Total Time = 16.667; // (usec) = 225 chars =  1800 Pixels
Hor Addr Time = 14.815; // (usec) = 200 chars =  1600 Pixels
Hor Blank Start = 14.815; // (usec) = 200 chars =  1600 Pixels
Hor Blank Time = 1.852; // (usec) = 25 chars =  200 Pixels
Hor Sync Start = 15.037; // (usec) = 203 chars =  1624 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.222; // (usec) = 3 chars =  24 Pixels
Hor Sync Time = 0.741; // (usec) = 10 chars =  80 Pixels
// H Back Porch = 0.889; // (usec) = 12 chars =  96 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 1000 lines  HT – (1.06xHA)
Ver Addr Time = 15.000; // (msec) = 900 lines  = 0.96
Ver Blank Start = 15.000; // (msec) = 900 lines  
Ver Blank Time = 1.667; // (msec) = 100 lines  
Ver Sync Start = 15.017; // (msec) = 901 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.017; // (msec) = 1 lines  
Ver Sync Time = 0.050; // (msec) = 3 lines  
// V Back Porch = 1.600; // (msec) = 96 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1600 x 1200 @ 60Hz;  
Hor Pixels = 1600; // Pixels  
Ver Pixels = 1200; // Lines  
Hor Frequency = 75.000; // kHz = 13.3 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 162.000; // MHz = 6.2 nsec  ± 0.5%
Character Width = 8; // Pixels = 49.4 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 25.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 13.333; // (usec) = 270 chars =  2160 Pixels
Hor Addr Time = 9.877; // (usec) = 200 chars =  1600 Pixels
Hor Blank Start = 9.877; // (usec) = 200 chars =  1600 Pixels
Hor Blank Time = 3.457; // (usec) = 70 chars =  560 Pixels
Hor Sync Start = 10.272; // (usec) = 208 chars =  1664 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.395; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 1.185; // (usec) = 24 chars =  192 Pixels
// H Back Porch = 1.877; // (usec) = 38 chars =  304 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 1250 lines  HT – (1.06xHA)
Ver Addr Time = 16.000; // (msec) = 1200 lines  = 2.86
Ver Blank Start = 16.000; // (msec) = 1200 lines  
Ver Blank Time = 0.667; // (msec) = 50 lines  
Ver Sync Start = 16.013; // (msec) = 1201 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.013; // (msec) = 1 lines  
Ver Sync Time = 0.040; // (msec) = 3 lines  
// V Back Porch = 0.613; // (msec) = 46 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1600 x 1200 @ 65Hz;  
Hor Pixels = 1600; // Pixels  
Ver Pixels = 1200; // Lines  
Hor Frequency = 81.250; // kHz = 12.3 usec /  line
Ver Frequency = 65.000; // Hz = 15.4 msec /  frame
Pixel Clock = 175.500; // MHz = 5.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 45.6 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 25.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 12.308; // (usec) = 270 chars =  2160 Pixels
Hor Addr Time = 9.117; // (usec) = 200 chars =  1600 Pixels
Hor Blank Start = 9.117; // (usec) = 200 chars =  1600 Pixels
Hor Blank Time = 3.191; // (usec) = 70 chars =  560 Pixels
Hor Sync Start = 9.481; // (usec) = 208 chars =  1664 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.365; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 1.094; // (usec) = 24 chars =  192 Pixels
// H Back Porch = 1.732; // (usec) = 38 chars =  304 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 15.385; // (msec) = 1250 lines  HT – (1.06xHA)
Ver Addr Time = 14.769; // (msec) = 1200 lines  = 2.64
Ver Blank Start = 14.769; // (msec) = 1200 lines  
Ver Blank Time = 0.615; // (msec) = 50 lines  
Ver Sync Start = 14.782; // (msec) = 1201 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.012; // (msec) = 1 lines  
Ver Sync Time = 0.037; // (msec) = 3 lines  
// V Back Porch = 0.566; // (msec) = 46 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1600 x 1200 @ 70Hz;  
Hor Pixels = 1600; // Pixels  
Ver Pixels = 1200; // Lines  
Hor Frequency = 87.500; // kHz = 11.4 usec /  line
Ver Frequency = 70.000; // Hz = 14.3 msec /  frame
Pixel Clock = 189.000; // MHz = 5.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 42.3 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 25.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 11.429; // (usec) = 270 chars =  2160 Pixels
Hor Addr Time = 8.466; // (usec) = 200 chars =  1600 Pixels
Hor Blank Start = 8.466; // (usec) = 200 chars =  1600 Pixels
Hor Blank Time = 2.963; // (usec) = 70 chars =  560 Pixels
Hor Sync Start = 8.804; // (usec) = 208 chars =  1664 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.339; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 1.016; // (usec) = 24 chars =  192 Pixels
// H Back Porch = 1.608; // (usec) = 38 chars =  304 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 14.286; // (msec) = 1250 lines  HT – (1.06xHA)
Ver Addr Time = 13.714; // (msec) = 1200 lines  = 2.46
Ver Blank Start = 13.714; // (msec) = 1200 lines  
Ver Blank Time = 0.571; // (msec) = 50 lines  
Ver Sync Start = 13.726; // (msec) = 1201 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.011; // (msec) = 1 lines  
Ver Sync Time = 0.034; // (msec) = 3 lines  
// V Back Porch = 0.526; // (msec) = 46 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1600 x 1200 @ 75Hz;  
Hor Pixels = 1600; // Pixels  
Ver Pixels = 1200; // Lines  
Hor Frequency = 93.750; // kHz = 10.7 usec /  line
Ver Frequency = 75.000; // Hz = 13.3 msec /  frame
Pixel Clock = 202.500; // MHz = 4.9 nsec  ± 0.5%
Character Width = 8; // Pixels = 39.5 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 25.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 10.667; // (usec) = 270 chars =  2160 Pixels
Hor Addr Time = 7.901; // (usec) = 200 chars =  1600 Pixels
Hor Blank Start = 7.901; // (usec) = 200 chars =  1600 Pixels
Hor Blank Time = 2.765; // (usec) = 70 chars =  560 Pixels
Hor Sync Start = 8.217; // (usec) = 208 chars =  1664 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.316; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 0.948; // (usec) = 24 chars =  192 Pixels
// H Back Porch = 1.501; // (usec) = 38 chars =  304 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.333; // (msec) = 1250 lines  HT – (1.06xHA)
Ver Addr Time = 12.800; // (msec) = 1200 lines  = 2.29
Ver Blank Start = 12.800; // (msec) = 1200 lines  
Ver Blank Time = 0.533; // (msec) = 50 lines  
Ver Sync Start = 12.811; // (msec) = 1201 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.011; // (msec) = 1 lines  
Ver Sync Time = 0.032; // (msec) = 3 lines  
// V Back Porch = 0.491; // (msec) = 46 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1600 x 1200 @ 85Hz;  
Hor Pixels = 1600; // Pixels  
Ver Pixels = 1200; // Lines  
Hor Frequency = 106.250; // kHz = 9.4 usec /  line
Ver Frequency = 85.000; // Hz = 11.8 msec /  frame
Pixel Clock = 229.500; // MHz = 4.4 nsec  ± 0.5%
Character Width = 8; // Pixels = 34.9 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 25.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 9.412; // (usec) = 270 chars =  2160 Pixels
Hor Addr Time = 6.972; // (usec) = 200 chars =  1600 Pixels
Hor Blank Start = 6.972; // (usec) = 200 chars =  1600 Pixels
Hor Blank Time = 2.440; // (usec) = 70 chars =  560 Pixels
Hor Sync Start = 7.251; // (usec) = 208 chars =  1664 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.279; // (usec) = 8 chars =  64 Pixels
Hor Sync Time = 0.837; // (usec) = 24 chars =  192 Pixels
// H Back Porch = 1.325; // (usec) = 38 chars =  304 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.765; // (msec) = 1250 lines  HT – (1.06xHA)
Ver Addr Time = 11.294; // (msec) = 1200 lines  = 2.02
Ver Blank Start = 11.294; // (msec) = 1200 lines  
Ver Blank Time = 0.471; // (msec) = 50 lines  
Ver Sync Start = 11.304; // (msec) = 1201 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.009; // (msec) = 1 lines  
Ver Sync Time = 0.028; // (msec) = 3 lines  
// V Back Porch = 0.433; // (msec) = 46 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1600 x 1200 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1600; // Pixels
Ver Pixels = 1200; // Lines
Hor Frequency = 152.415; // kHz = 6.6 usec / line
Ver Frequency = 119.917; // Hz = 8.3 msec / frame
Pixel Clock = 268.250; // MHz = 3.7 nsec ± 0.5%
Character Width = 8; // Pixels = 29.8 nsec
Scan Type = NONINTERLACED; // H Phase = 0.9 %
Hor Sync Polarity = POSITIVE; // HBlank = 9.1% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 6.561; // (usec) = 220 chars = 1760 Pixels
Hor Addr Time = 5.965; // (usec) = 200 chars = 1600 Pixels
Hor Blank Start = 5.965; // (usec) = 200 chars = 1600 Pixels
Hor Blank Time = 0.596; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 6.144; // (usec) = 206 chars = 1648 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.179; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.119; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.298; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.339; // (msec) = 1271 lines HT – (1.06xHA)
Ver Addr Time = 7.873; // (msec) = 1200 lines = 0.24
Ver Blank Start = 7.873; // (msec) = 1200 lines
Ver Blank Time = 0.466; // (msec) = 71 lines
Ver Sync Start = 7.893; // (msec) = 1203 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.020; // (msec) = 3 lines
Ver Sync Time = 0.026; // (msec) = 4 lines
// V Back Porch = 0.420; // (msec) = 64 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters
Timing Name = 1680 x 1050 @ 60Hz CVT (Reduced Blanking);
Hor Pixels = 1680; // Pixels
Ver Pixels = 1050; // Lines
Hor Frequency = 64.674; // kHz = 15.5 usec / line
Ver Frequency = 59.883; // Hz = 16.7 msec / frame
Pixel Clock = 119.000; // MHz = 8.4 nsec ± 0.5%
Character Width = 8; // Pixels = 67.2 nsec
Scan Type = NONINTERLACED; // H Phase = 0.9 %
Hor Sync Polarity = POSITIVE; // HBlank = 8.7% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 2.8% of VTotal
Hor Total Time = 15.462; // (usec) = 230 chars = 1840 Pixels
Hor Addr Time = 14.118; // (usec) = 210 chars = 1680 Pixels
Hor Blank Start = 14.118; // (usec) = 210 chars = 1680 Pixels
Hor Blank Time = 1.345; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 14.521; // (usec) = 216 chars = 1728 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.403; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.269; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.672; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 16.699; // (msec) = 1080 lines HT – (1.06xHA)
Ver Addr Time = 16.235; // (msec) = 1050 lines = 0.5
Ver Blank Start = 16.235; // (msec) = 1050 lines
Ver Blank Time = 0.464; // (msec) = 30 lines
Ver Sync Start = 16.282; // (msec) = 1053 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.046; // (msec) = 3 lines
Ver Sync Time = 0.093; // (msec) = 6 lines
// V Back Porch = 0.325; // (msec) = 21 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1680 x 1050 @ 60Hz;  
Hor Pixels = 1680; // Pixels  
Ver Pixels = 1050; // Lines  
Hor Frequency = 65.290; // kHz = 15.3 usec /  line
Ver Frequency = 59.954; // Hz = 16.7 msec /  frame
Pixel Clock = 146.250; // MHz = 6.8 nsec  ± 0.5%
Character Width = 8; // Pixels = 54.7 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.9 %
Hor Sync Polarity = NEGATIVE // HBlank = 25.0% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.6% of VTotal  
Hor Total Time = 15.316; // (usec) = 280 chars =  2240 Pixels
Hor Addr Time = 11.487; // (usec) = 210 chars =  1680 Pixels
Hor Blank Start = 11.487; // (usec) = 210 chars =  1680 Pixels
Hor Blank Time = 3.829; // (usec) = 70 chars =  560 Pixels
Hor Sync Start = 12.198; // (usec) = 223 chars =  1784 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.711; // (usec) = 13 chars =  104 Pixels
Hor Sync Time = 1.203; // (usec) = 22 chars =  176 Pixels
// H Back Porch = 1.915; // (usec) = 35 chars =  280 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.679; // (msec) = 1089 lines  HT – (1.06xHA)
Ver Addr Time = 16.082; // (msec) = 1050 lines  = 3.14
Ver Blank Start = 16.082; // (msec) = 1050 lines  
Ver Blank Time = 0.597; // (msec) = 39 lines  
Ver Sync Start = 16.128; // (msec) = 1053 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.046; // (msec) = 3 lines  
Ver Sync Time = 0.092; // (msec) = 6 lines  
// V Back Porch = 0.459; // (msec) = 30 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1680 x 1050 @ 75Hz;  
Hor Pixels = 1680; // Pixels  
Ver Pixels = 1050; // Lines  
Hor Frequency = 82.306; // kHz = 12.1 usec /  line
Ver Frequency = 74.892; // Hz = 13.4 msec /  frame
Pixel Clock = 187.000; // MHz = 5.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 42.8 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.9 %
Hor Sync Polarity = NEGATIVE // HBlank = 26.1% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.5% of VTotal  
Hor Total Time = 12.150; // (usec) = 284 chars =  2272 Pixels
Hor Addr Time = 8.984; // (usec) = 210 chars =  1680 Pixels
Hor Blank Start = 8.984; // (usec) = 210 chars =  1680 Pixels
Hor Blank Time = 3.166; // (usec) = 74 chars =  592 Pixels
Hor Sync Start = 9.626; // (usec) = 225 chars =  1800 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.642; // (usec) = 15 chars =  120 Pixels
Hor Sync Time = 0.941; // (usec) = 22 chars =  176 Pixels
// H Back Porch = 1.583; // (usec) = 37 chars =  296 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.353; // (msec) = 1099 lines  HT – (1.06xHA)
Ver Addr Time = 12.757; // (msec) = 1050 lines  = 2.63
Ver Blank Start = 12.757; // (msec) = 1050 lines  
Ver Blank Time = 0.595; // (msec) = 49 lines  
Ver Sync Start = 12.794; // (msec) = 1053 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.036; // (msec) = 3 lines  
Ver Sync Time = 0.073; // (msec) = 6 lines  
// V Back Porch = 0.486; // (msec) = 40 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1680 x 1050 @ 85Hz;  
Hor Pixels = 1680; // Pixels  
Ver Pixels = 1050; // Lines  
Hor Frequency = 93.859; // kHz = 10.7 usec /  line
Ver Frequency = 84.941; // Hz = 11.8 msec /  frame
Pixel Clock = 214.750; // MHz = 4.7 nsec  ± 0.5%
Character Width = 8; // Pixels = 37.3 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.8 %
Hor Sync Polarity = NEGATIVE // HBlank = 26.6% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 5.0% of VTotal  
Hor Total Time = 10.654; // (usec) = 286 chars =  2288 Pixels
Hor Addr Time = 7.823; // (usec) = 210 chars =  1680 Pixels
Hor Blank Start = 7.823; // (usec) = 210 chars =  1680 Pixels
Hor Blank Time = 2.831; // (usec) = 76 chars =  608 Pixels
Hor Sync Start = 8.419; // (usec) = 226 chars =  1808 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.596; // (usec) = 16 chars =  128 Pixels
Hor Sync Time = 0.820; // (usec) = 22 chars =  176 Pixels
// H Back Porch = 1.416; // (usec) = 38 chars =  304 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.773; // (msec) = 1105 lines  HT – (1.06xHA)
Ver Addr Time = 11.187; // (msec) = 1050 lines  = 2.36
Ver Blank Start = 11.187; // (msec) = 1050 lines  
Ver Blank Time = 0.586; // (msec) = 55 lines  
Ver Sync Start = 11.219; // (msec) = 1053 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.032; // (msec) = 3 lines  
Ver Sync Time = 0.064; // (msec) = 6 lines  
// V Back Porch = 0.490; // (msec) = 46 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1680 x 1050 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1680; // Pixels
Ver Pixels = 1050; // Lines
Hor Frequency = 133.424; // kHz = 7.5 usec / line
Ver Frequency = 119.986; // Hz = 8.3 msec / frame
Pixel Clock = 245.500; // MHz = 4.1 nsec ± 0.5%
Character Width = 8; // Pixels = 32.6 nsec
Scan Type = NONINTERLACED; // H Phase = 0.9 %
Hor Sync Polarity = POSITIVE; // HBlank = 8.7% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 7.495; // (usec) = 230 chars = 1840 Pixels
Hor Addr Time = 6.843; // (usec) = 210 chars = 1680 Pixels
Hor Blank Start = 6.843; // (usec) = 210 chars = 1680 Pixels
Hor Blank Time = 0.652; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 7.039; // (usec) = 216 chars = 1728 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.196; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.130; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.326; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.334; // (msec) = 1112 lines HT – (1.06xHA)
Ver Addr Time = 7.870; // (msec) = 1050 lines = 0.24
Ver Blank Start = 7.870; // (msec) = 1050 lines
Ver Blank Time = 0.465; // (msec) = 62 lines
Ver Sync Start = 7.892; // (msec) = 1053 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.022; // (msec) = 3 lines
Ver Sync Time = 0.045; // (msec) = 6 lines
// V Back Porch = 0.397; // (msec) = 53 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1792 x 1344 @ 60 Hz  
Hor Pixels = 1792; // Pixels  
Ver Pixels = 1344; // Lines  
Hor Frequency = 83.640; // kHz = 12.0 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 204.750; // MHz = 4.9 nsec  ± 0.5%
Character Width = 8; // Pixels = 39.1 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.1 %
Hor Sync Polarity = NEGATIVE; // HBlank = 26.8% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.6% of VTotal  
Hor Total Time = 11.956; // (usec) = 306 chars =  2448 Pixels
Hor Addr Time = 8.752; // (usec) = 224 chars =  1792 Pixels
Hor Blank Start = 8.752; // (usec) = 224 chars =  1792 Pixels
Hor Blank Time = 3.204; // (usec) = 82 chars =  656 Pixels
Hor Sync Start = 9.377; // (usec) = 240 chars =  1920 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.625; // (usec) = 16 chars =  128 Pixels
Hor Sync Time = 0.977; // (usec) = 25 chars =  200 Pixels
// H Back Porch = 1.602; // (usec) = 41 chars =  328 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 1394 lines  HT – (1.06xHA)
Ver Addr Time = 16.069; // (msec) = 1344 lines  = 2.68
Ver Blank Start = 16.069; // (msec) = 1344 lines  
Ver Blank Time = 0.598; // (msec) = 50 lines  
Ver Sync Start = 16.081; // (msec) = 1345 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.012; // (msec) = 1 lines  
Ver Sync Time = 0.036; // (msec) = 3 lines  
// V Back Porch = 0.550; // (msec) = 46 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1792 x 1344 @ 75Hz;  
Hor Pixels = 1792; // Pixels  
Ver Pixels = 1344; // Lines  
Hor Frequency = 106.270; // kHz = 9.4 usec /  line
Ver Frequency = 74.997; // Hz = 13.3 msec /  frame
Pixel Clock = 261.000; // MHz = 3.8 nsec  ± 0.5%
Character Width = 8; // Pixels = 30.7 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.2 %
Hor Sync Polarity = NEGATIVE; // HBlank = 27.0% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 5.2% of VTotal  
Hor Total Time = 9.410; // (usec) = 307 chars =  2456 Pixels
Hor Addr Time = 6.866; // (usec) = 224 chars =  1792 Pixels
Hor Blank Start = 6.866; // (usec) = 224 chars =  1792 Pixels
Hor Blank Time = 2.544; // (usec) = 83 chars =  664 Pixels
Hor Sync Start = 7.234; // (usec) = 236 chars =  1888 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.368; // (usec) = 12 chars =  96 Pixels
Hor Sync Time = 0.828; // (usec) = 27 chars =  216 Pixels
// H Back Porch = 1.349; // (usec) = 44 chars =  352 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.334; // (msec) = 1417 lines  HT – (1.06xHA)
Ver Addr Time = 12.647; // (msec) = 1344 lines  = 2.13
Ver Blank Start = 12.647; // (msec) = 1344 lines  
Ver Blank Time = 0.687; // (msec) = 73 lines  
Ver Sync Start = 12.656; // (msec) = 1345 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.009; // (msec) = 1 lines  
Ver Sync Time = 0.028; // (msec) = 3 lines  
// V Back Porch = 0.649; // (msec) = 69 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1792 x 1344 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1792; // Pixels
Ver Pixels = 1344; // Lines
Hor Frequency = 170.722; // kHz = 5.9 usec / line
Ver Frequency = 119.974; // Hz = 8.3 msec / frame
Pixel Clock = 333.250; // MHz = 3.0 nsec ± 0.5%
Character Width = 8; // Pixels = 24.0 nsec
Scan Type = NONINTERLACED; // H Phase = 0.8 %
Hor Sync Polarity = POSITIVE; // HBlank = 8.2% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 5.857; // (usec) = 244 chars = 1952 Pixels
Hor Addr Time = 5.377; // (usec) = 224 chars = 1792 Pixels
Hor Blank Start = 5.377; // (usec) = 224 chars = 1792 Pixels
Hor Blank Time = 0.480; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 5.521; // (usec) = 230 chars = 1840 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.144; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.096; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.240; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.335; // (msec) = 1423 lines HT – (1.06xHA)
Ver Addr Time = 7.872; // (msec) = 1344 lines = 0.16
Ver Blank Start = 7.872; // (msec) = 1344 lines
Ver Blank Time = 0.463; // (msec) = 79 lines
Ver Sync Start = 7.890; // (msec) = 1347 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.018; // (msec) = 3 lines
Ver Sync Time = 0.023; // (msec) = 4 lines
// V Back Porch = 0.422; // (msec) = 72 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1856 x 1392 at 60Hz;  
Hor Pixels = 1856; // Pixels  
Ver Pixels = 1392; // Lines  
Hor Frequency = 86.333; // kHz = 11.6 usec /  line
Ver Frequency = 59.995; // Hz = 16.7 msec /  frame
Pixel Clock = 218.250; // MHz = 4.6 nsec  ± 0.5%
Character Width = 8; // Pixels = 36.7 nsec  
Scan Type = NONINTERLACED; // H Phase =  5.1 %
Hor Sync Polarity = NEGATIVE; // HBlank = 26.6% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.3% of VTotal  
Hor Total Time = 11.583; // (usec) = 316 chars =  2528 Pixels
Hor Addr Time = 8.504; // (usec) = 232 chars =  1856 Pixels
Hor Blank Start = 8.504; // (usec) = 232 chars =  1856 Pixels
Hor Blank Time = 3.079; // (usec) = 84 chars =  672 Pixels
Hor Sync Start = 8.944; // (usec) = 244 chars =  1952 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.440; // (usec) = 12 chars =  96 Pixels
Hor Sync Time = 1.026; // (usec) = 28 chars =  224 Pixels
// H Back Porch = 1.613; // (usec) = 44 chars =  352 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.668; // (msec) = 1439 lines  HT – (1.06xHA)
Ver Addr Time = 16.124; // (msec) = 1392 lines  = 2.57
Ver Blank Start = 16.124; // (msec) = 1392 lines  
Ver Blank Time = 0.544; // (msec) = 47 lines  
Ver Sync Start = 16.135; // (msec) = 1393 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.012; // (msec) = 1 lines  
Ver Sync Time = 0.035; // (msec) = 3 lines  
// V Back Porch = 0.498; // (msec) = 43 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1856 x 1392 @ 75Hz;  
Hor Pixels = 1856; // Pixels  
Ver Pixels = 1392; // Lines  
Hor Frequency = 112.500; // kHz = 8.9 usec /  line
Ver Frequency = 75.000; // Hz = 13.3 msec /  frame
Pixel Clock = 288.000; // MHz = 3.5 nsec  ± 0.5%
Character Width = 8; // Pixels = 27.8 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.4 %
Hor Sync Polarity = NEGATIVE; // HBlank = 27.5% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 7.2% of VTotal  
Hor Total Time = 8.889; // (usec) = 320 chars =  2560 Pixels
Hor Addr Time = 6.444; // (usec) = 232 chars =  1856 Pixels
Hor Blank Start = 6.444; // (usec) = 232 chars =  1856 Pixels
Hor Blank Time = 2.444; // (usec) = 88 chars =  704 Pixels
Hor Sync Start = 6.889; // (usec) = 248 chars =  1984 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.444; // (usec) = 16 chars =  128 Pixels
Hor Sync Time = 0.778; // (usec) = 28 chars =  224 Pixels
// H Back Porch = 1.222; // (usec) = 44 chars =  352 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.333; // (msec) = 1500 lines  HT – (1.06xHA)
Ver Addr Time = 12.373; // (msec) = 1392 lines  = 2.06
Ver Blank Start = 12.373; // (msec) = 1392 lines  
Ver Blank Time = 0.960; // (msec) = 108 lines  
Ver Sync Start = 12.382; // (msec) = 1393 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.009; // (msec) = 1 lines  
Ver Sync Time = 0.027; // (msec) = 3 lines  
// V Back Porch = 0.924; // (msec) = 104 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1856 x 1392 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1856; // Pixels
Ver Pixels = 1392; // Lines
Hor Frequency = 176.835; // kHz = 5.7 usec / line
Ver Frequency = 119.970; // Hz = 8.3 msec / frame
Pixel Clock = 356.500; // MHz = 2.8 nsec ± 0.5%
Character Width = 8; // Pixels = 22.4 nsec
Scan Type = NONINTERLACED; // H Phase = 0.8 %
Hor Sync Polarity = POSITIVE; // HBlank = 7.9% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 5.655; // (usec) = 252 chars = 2016 Pixels
Hor Addr Time = 5.206; // (usec) = 232 chars = 1856 Pixels
Hor Blank Start = 5.206; // (usec) = 232 chars = 1856 Pixels
Hor Blank Time = 0.449; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 5.341; // (usec) = 238 chars = 1904 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.135; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.090; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.224; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.335; // (msec) = 1474 lines HT – (1.06xHA)
Ver Addr Time = 7.872; // (msec) = 1392 lines = 0.14
Ver Blank Start = 7.872; // (msec) = 1392 lines
Ver Blank Time = 0.464; // (msec) = 82 lines
Ver Sync Start = 7.889; // (msec) = 1395 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.017; // (msec) = 3 lines
Ver Sync Time = 0.023; // (msec) = 4 lines
// V Back Porch = 0.424; // (msec) = 75 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1920 x 1080 @ 60Hz;  
Hor Pixels = 1920; // Pixels  
Ver Pixels = 1080; // Lines  
Hor Frequency = 67.500; // kHz = 14.8 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 148.500; // MHz = 6.7 nsec  ± 0.5%
Character Width = 4; // Pixels = 26.9 nsec  
Scan Type = NONINTERLACED; // H Phase =  1.4 %
Hor Sync Polarity = POSITIVE // HBlank = 12.7% of HTotal  
Ver Sync Polarity = POSITIVE // VBlank = 4.0% of VTotal  
Hor Total Time = 14.815; // (usec) = 550 chars =  2200 Pixels
Hor Addr Time = 12.929; // (usec) = 480 chars =  1920 Pixels
Hor Blank Start = 12.929; // (usec) = 480 chars =  1920 Pixels
Hor Blank Time = 1.886; // (usec) = 70 chars =  280 Pixels
Hor Sync Start = 13.522; // (usec) = 502 chars =  2008 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.593; // (usec) = 22 chars =  88 Pixels
Hor Sync Time = 0.296; // (usec) = 11 chars =  44 Pixels
// H Back Porch = 0.997; // (usec) = 37 chars =  148 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 1125 lines  HT – (1.06xHA)
Ver Addr Time = 16.000; // (msec) = 1080 lines  = 1.11
Ver Blank Start = 16.000; // (msec) = 1080 lines  
Ver Blank Time = 0.667; // (msec) = 45 lines  
Ver Sync Start = 16.059; // (msec) = 1084 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.059; // (msec) = 4 lines  
Ver Sync Time = 0.074; // (msec) = 5 lines  
  Detailed Timing Parameters
Timing Name = 1920 x 1200 @ 60Hz CVT (Reduced Blanking);
Hor Pixels = 1920; // Pixels
Ver Pixels = 1200; // Lines
Hor Frequency = 74.038; // kHz = 13.5 usec / line
Ver Frequency = 59.950; // Hz = 16.7 msec / frame
Pixel Clock = 154.000; // MHz = 6.5 nsec ± 0.5%
Character Width = 8; // Pixels = 51.9 nsec
Scan Type = NONINTERLACED; // H Phase = 0.8 %
Hor Sync Polarity = POSITIVE; // HBlank = 7.7% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 2.8% of VTotal
Hor Total Time = 13.506; // (usec) = 260 chars = 2080 Pixels
Hor Addr Time = 12.468; // (usec) = 240 chars = 1920 Pixels
Hor Blank Start = 12.468; // (usec) = 240 chars = 1920 Pixels
Hor Blank Time = 1.039; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 12.779; // (usec) = 246 chars = 1968 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.312; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.208; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.519; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 16.681; // (msec) = 1235 lines HT – (1.06xHA)
Ver Addr Time = 16.208; // (msec) = 1200 lines = 0.29
Ver Blank Start = 16.208; // (msec) = 1200 lines
Ver Blank Time = 0.473; // (msec) = 35 lines
Ver Sync Start = 16.248; // (msec) = 1203 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.041; // (msec) = 3 lines
Ver Sync Time = 0.081; // (msec) = 6 lines
// V Back Porch = 0.351; // (msec) = 26 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1920 x 1200 @ 60Hz;  
Hor Pixels = 1920; // Pixels  
Ver Pixels = 1200; // Lines  
Hor Frequency = 74.556; // kHz = 13.4 usec /  line
Ver Frequency = 59.885; // Hz = 16.7 msec /  frame
Pixel Clock = 193.250; // MHz = 5.2 nsec  ± 0.5%
Character Width = 8; // Pixels = 41.4 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.9 %
Hor Sync Polarity = NEGATIVE // HBlank = 25.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.6% of VTotal  
Hor Total Time = 13.413; // (usec) = 324 chars =  2592 Pixels
Hor Addr Time = 9.935; // (usec) = 240 chars =  1920 Pixels
Hor Blank Start = 9.935; // (usec) = 240 chars =  1920 Pixels
Hor Blank Time = 3.477; // (usec) = 84 chars =  672 Pixels
Hor Sync Start = 10.639; // (usec) = 257 chars =  2056 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.704; // (usec) = 17 chars =  136 Pixels
Hor Sync Time = 1.035; // (usec) = 25 chars =  200 Pixels
// H Back Porch = 1.739; // (usec) = 42 chars =  336 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.699; // (msec) = 1245 lines  HT – (1.06xHA)
Ver Addr Time = 16.095; // (msec) = 1200 lines  = 2.88
Ver Blank Start = 16.095; // (msec) = 1200 lines  
Ver Blank Time = 0.604; // (msec) = 45 lines  
Ver Sync Start = 16.135; // (msec) = 1203 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.040; // (msec) = 3 lines  
Ver Sync Time = 0.080; // (msec) = 6 lines  
// V Back Porch = 0.483; // (msec) = 36 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1920 x 1200 @ 75Hz;  
Hor Pixels = 1920; // Pixels  
Ver Pixels = 1200; // Lines  
Hor Frequency = 94.038; // kHz = 10.6 usec /  line
Ver Frequency = 74.930; // Hz = 13.3 msec /  frame
Pixel Clock = 245.250; // MHz = 4.1 nsec  ± 0.5%
Character Width = 8; // Pixels = 32.6 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE // HBlank = 26.4% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.4% of VTotal  
Hor Total Time = 10.634; // (usec) = 326 chars =  2608 Pixels
Hor Addr Time = 7.829; // (usec) = 240 chars =  1920 Pixels
Hor Blank Start = 7.829; // (usec) = 240 chars =  1920 Pixels
Hor Blank Time = 2.805; // (usec) = 86 chars =  688 Pixels
Hor Sync Start = 8.383; // (usec) = 257 chars =  2056 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.555; // (usec) = 17 chars =  136 Pixels
Hor Sync Time = 0.848; // (usec) = 26 chars =  208 Pixels
// H Back Porch = 1.403; // (usec) = 43 chars =  344 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.346; // (msec) = 1255 lines  HT – (1.06xHA)
Ver Addr Time = 12.761; // (msec) = 1200 lines  = 2.34
Ver Blank Start = 12.761; // (msec) = 1200 lines  
Ver Blank Time = 0.585; // (msec) = 55 lines  
Ver Sync Start = 12.793; // (msec) = 1203 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.032; // (msec) = 3 lines  
Ver Sync Time = 0.064; // (msec) = 6 lines  
// V Back Porch = 0.489; // (msec) = 46 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1920 x 1200 @ 85Hz;  
Hor Pixels = 1920; // Pixels  
Ver Pixels = 1200; // Lines  
Hor Frequency = 107.184; // kHz = 9.3 usec /  line
Ver Frequency = 84.932; // Hz = 11.8 msec /  frame
Pixel Clock = 281.250; // MHz = 3.6 nsec  ± 0.5%
Character Width = 8; // Pixels = 28.4 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE // HBlank = 26.8% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.9% of VTotal  
Hor Total Time = 9.330; // (usec) = 328 chars =  2624 Pixels
Hor Addr Time = 6.827; // (usec) = 240 chars =  1920 Pixels
Hor Blank Start = 6.827; // (usec) = 240 chars =  1920 Pixels
Hor Blank Time = 2.503; // (usec) = 88 chars =  704 Pixels
Hor Sync Start = 7.339; // (usec) = 258 chars =  2064 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.512; // (usec) = 18 chars =  144 Pixels
Hor Sync Time = 0.740; // (usec) = 26 chars =  208 Pixels
// H Back Porch = 1.252; // (usec) = 44 chars =  352 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.774; // (msec) = 1262 lines  HT – (1.06xHA)
Ver Addr Time = 11.196; // (msec) = 1200 lines  = 2.09
Ver Blank Start = 11.196; // (msec) = 1200 lines  
Ver Blank Time = 0.578; // (msec) = 62 lines  
Ver Sync Start = 11.224; // (msec) = 1203 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.028; // (msec) = 3 lines  
Ver Sync Time = 0.056; // (msec) = 6 lines  
// V Back Porch = 0.494; // (msec) = 53 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1920 x 1200 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1920; // Pixels
Ver Pixels = 1200; // Lines
Hor Frequency = 152.404; // kHz = 6.6 usec / line
Ver Frequency = 119.909; // Hz = 8.3 msec / frame
Pixel Clock = 317.000; // MHz = 3.2 nsec ± 0.5%
Character Width = 8; // Pixels = 25.2 nsec
Scan Type = NONINTERLACED; // H Phase = 0.8 %
Hor Sync Polarity = POSITIVE; // HBlank = 7.7% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 6.562; // (usec) = 260 chars = 2080 Pixels
Hor Addr Time = 6.057; // (usec) = 240 chars = 1920 Pixels
Hor Blank Start = 6.057; // (usec) = 240 chars = 1920 Pixels
Hor Blank Time = 0.505; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 6.208; // (usec) = 246 chars = 1968 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.151; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.101; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.252; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.340; // (msec) = 1271 lines HT – (1.06xHA)
Ver Addr Time = 7.874; // (msec) = 1200 lines = 0.14
Ver Blank Start = 7.874; // (msec) = 1200 lines
Ver Blank Time = 0.466; // (msec) = 71 lines
Ver Sync Start = 7.894; // (msec) = 1203 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.020; // (msec) = 3 lines
Ver Sync Time = 0.039; // (msec) = 6 lines
// V Back Porch = 0.407; // (msec) = 62 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 1920 x 1440 @ 60Hz;  
Hor Pixels = 1920; // Pixels  
Ver Pixels = 1440; // Lines  
Hor Frequency = 90.000; // kHz = 11.1 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 234.000; // MHz = 4.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 34.2 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.2 %
Hor Sync Polarity = NEGATIVE; // HBlank = 26.2% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 11.111; // (usec) = 325 chars =  2600 Pixels
Hor Addr Time = 8.205; // (usec) = 240 chars =  1920 Pixels
Hor Blank Start = 8.205; // (usec) = 240 chars =  1920 Pixels
Hor Blank Time = 2.906; // (usec) = 85 chars =  680 Pixels
Hor Sync Start = 8.752; // (usec) = 256 chars =  2048 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.547; // (usec) = 16 chars =  128 Pixels
Hor Sync Time = 0.889; // (usec) = 26 chars =  208 Pixels
// H Back Porch = 1.470; // (usec) = 43 chars =  344 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 1500 lines  HT – (1.06xHA)
Ver Addr Time = 16.000; // (msec) = 1440 lines  = 2.41
Ver Blank Start = 16.000; // (msec) = 1440 lines  
Ver Blank Time = 0.667; // (msec) = 60 lines  
Ver Sync Start = 16.011; // (msec) = 1441 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.011; // (msec) = 1 lines  
Ver Sync Time = 0.033; // (msec) = 3 lines  
// V Back Porch = 0.622; // (msec) = 56 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 1920 x 1440 @ 75Hz;  
Hor Pixels = 1920; // Pixels  
Ver Pixels = 1440; // Lines  
Hor Frequency = 112.500; // kHz = 8.9 usec /  line
Ver Frequency = 75.000; // Hz = 13.3 msec /  frame
Pixel Clock = 297.000; // MHz = 3.4 nsec  ± 0.5%
Character Width = 8; // Pixels = 26.9 nsec  
Scan Type = NONINTERLACED; // H Phase =  3.9 %
Hor Sync Polarity = NEGATIVE // HBlank = 27.3% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 8.889; // (usec) = 330 chars =  2640 Pixels
Hor Addr Time = 6.465; // (usec) = 240 chars =  1920 Pixels
Hor Blank Start = 6.465; // (usec) = 240 chars =  1920 Pixels
Hor Blank Time = 2.424; // (usec) = 90 chars =  720 Pixels
Hor Sync Start = 6.949; // (usec) = 258 chars =  2064 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.485; // (usec) = 18 chars =  144 Pixels
Hor Sync Time = 0.754; // (usec) = 28 chars =  224 Pixels
// H Back Porch = 1.185; // (usec) = 44 chars =  352 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.333; // (msec) = 1500 lines  HT – (1.06xHA)
Ver Addr Time = 12.800; // (msec) = 1440 lines  = 2.04
Ver Blank Start = 12.800; // (msec) = 1440 lines  
Ver Blank Time = 0.533; // (msec) = 60 lines  
Ver Sync Start = 12.809; // (msec) = 1441 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.009; // (msec) = 1 lines  
Ver Sync Time = 0.027; // (msec) = 3 lines  
// V Back Porch = 0.498; // (msec) = 56 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 1920 x 1440 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 1920; // Pixels
Ver Pixels = 1440; // Lines
Hor Frequency = 182.933; // kHz = 5.5 usec / line
Ver Frequency = 119.956; // Hz = 8.3 msec / frame
Pixel Clock = 380.500; // MHz = 2.6 nsec ± 0.5%
Character Width = 8; // Pixels = 21.0 nsec
Scan Type = NONINTERLACED; // H Phase = 0.8 %
Hor Sync Polarity = POSITIVE; // HBlank = 7.7% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.6% of VTotal
Hor Total Time = 5.466; // (usec) = 260 chars = 2080 Pixels
Hor Addr Time = 5.046; // (usec) = 240 chars = 1920 Pixels
Hor Blank Start = 5.046; // (usec) = 240 chars = 1920 Pixels
Hor Blank Time = 0.420; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 5.172; // (usec) = 246 chars = 1968 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.126; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.084; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.210; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.336; // (msec) = 1525 lines HT – (1.06xHA)
Ver Addr Time = 7.872; // (msec) = 1440 lines = 0.12
Ver Blank Start = 7.872; // (msec) = 1440 lines
Ver Blank Time = 0.465; // (msec) = 85 lines
Ver Sync Start = 7.888; // (msec) = 1443 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.016; // (msec) = 3 lines
Ver Sync Time = 0.022; // (msec) = 4 lines
// V Back Porch = 0.426; // (msec) = 78 lines
// V Top Border = 0.000; // (msec) = 0 lines
    
  Detailed Timing Parameters  
Timing Name = 2048 x 1152 @ 60Hz;  
Hor Pixels = 2048; // Pixels  
Ver Pixels = 1152; // Lines  
Hor Frequency = 72.000; // KHz = 13.9 usec /  line
Ver Frequency = 60.000; // Hz = 16.7 msec /  frame
Pixel Clock = 162.000; // MHz = 6.2 nsec  ± 0.5%
Character Width = 1; // Pixels = 6.2 nsec  
Scan Type = NONINTERLACED; // H Phase =  1.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 9.0% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.0% of VTotal  
Hor Total Time = 13.889; // (usec) = 2250 chars =  2250 Pixels
Hor Addr Time = 12.642; // (usec) = 2048 chars =  2048 Pixels
Hor Blank Start = 12.642; // (usec) = 2048 chars =  2048 Pixels
Hor Blank Time = 1.247; // (usec) = 202 chars =  202 Pixels
Hor Sync Start = 12.802; // (usec) = 2074 chars =  2074 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.160; // (usec) = 26 chars =  26 Pixels
Hor Sync Time = 0.494; // (usec) = 80 chars =  80 Pixels
// H Back Porch = 0.593; // (usec) = 96 chars =  96 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.667; // (msec) = 1200 lines  HT – (1.06xHA)
Ver Addr Time = 16.000; // (msec) = 1152 lines  = 0.49
Ver Blank Start = 16.000; // (msec) = 1152 lines  
Ver Blank Time = 0.667; // (msec) = 48 lines  
Ver Sync Start = 16.014; // (msec) = 1153 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.014; // (msec) = 1 lines  
Ver Sync Time = 0.042; // (msec) = 3 lines  
// V Back Porch = 0.611; // (msec) = 44 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 2560 x 1600 @ 60Hz CVT (Reduced Blanking);
Hor Pixels = 2560; // Pixels
Ver Pixels = 1600; // Lines
Hor Frequency = 98.713; // kHz = 10.1 usec / line
Ver Frequency = 59.972; // Hz = 16.7 msec / frame
Pixel Clock = 268.500; // MHz = 3.7 nsec ± 0.5%
Character Width = 8; // Pixels = 29.8 nsec
Scan Type = NONINTERLACED; // H Phase = 0.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 5.9% of HTotal
Ver Sync Polarity = NEGATIVE; // VBlank = 2.8% of VTotal
Hor Total Time = 10.130; // (usec) = 340 chars = 2720 Pixels
Hor Addr Time = 9.534; // (usec) = 320 chars = 2560 Pixels
Hor Blank Start = 9.534; // (usec) = 320 chars = 2560 Pixels
Hor Blank Time = 0.596; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 9.713; // (usec) = 326 chars = 2608 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.179; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.119; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.298; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 16.675; // (msec) = 1646 lines HT – (1.06xHA)
Ver Addr Time = 16.209; // (msec) = 1600 lines = 0.02
Ver Blank Start = 16.209; // (msec) = 1600 lines
Ver Blank Time = 0.466; // (msec) = 46 lines
Ver Sync Start = 16.239; // (msec) = 1603 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.030; // (msec) = 3 lines
Ver Sync Time = 0.061; // (msec) = 6 lines
// V Back Porch = 0.375; // (msec) = 37 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters  
Timing Name = 2560 x 1600 @ 60Hz;  
Hor Pixels = 2560; // Pixels  
Ver Pixels = 1600; // Lines  
Hor Frequency = 99.458; // kHz = 10.1 usec /  line
Ver Frequency = 59.987; // Hz = 16.7 msec /  frame
Pixel Clock = 348.500; // MHz = 2.9 nsec  ± 0.5%
Character Width = 8; // Pixels = 23.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE // HBlank = 26.9% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 3.5% of VTotal  
Hor Total Time = 10.055; // (usec) = 438 chars =  3504 Pixels
Hor Addr Time = 7.346; // (usec) = 320 chars =  2560 Pixels
Hor Blank Start = 7.346; // (usec) = 320 chars =  2560 Pixels
Hor Blank Time = 2.709; // (usec) = 118 chars =  944 Pixels
Hor Sync Start = 7.897; // (usec) = 344 chars =  2752 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.551; // (usec) = 24 chars =  192 Pixels
Hor Sync Time = 0.803; // (usec) = 35 chars =  280 Pixels
// H Back Porch = 1.354; // (usec) = 59 chars =  472 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 16.670; // (msec) = 1658 lines  HT – (1.06xHA)
Ver Addr Time = 16.087; // (msec) = 1600 lines  = 2.27
Ver Blank Start = 16.087; // (msec) = 1600 lines  
Ver Blank Time = 0.583; // (msec) = 58 lines  
Ver Sync Start = 16.117; // (msec) = 1603 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.030; // (msec) = 3 lines  
Ver Sync Time = 0.060; // (msec) = 6 lines  
// V Back Porch = 0.493; // (msec) = 49 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 2560 x 1600 @ 75Hz;  
Hor Pixels = 2560; // Pixels  
Ver Pixels = 1600; // Lines  
Hor Frequency = 125.354; // kHz = 8.0 usec /  line
Ver Frequency = 74.972; // Hz = 13.3 msec /  frame
Pixel Clock = 443.250; // MHz = 2.3 nsec  ± 0.5%
Character Width = 8; // Pixels = 18.0 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE // HBlank = 27.6% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.3% of VTotal  
Hor Total Time = 7.977; // (usec) = 442 chars =  3536 Pixels
Hor Addr Time = 5.776; // (usec) = 320 chars =  2560 Pixels
Hor Blank Start = 5.776; // (usec) = 320 chars =  2560 Pixels
Hor Blank Time = 2.202; // (usec) = 122 chars =  976 Pixels
Hor Sync Start = 6.245; // (usec) = 346 chars =  2768 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.469; // (usec) = 26 chars =  208 Pixels
Hor Sync Time = 0.632; // (usec) = 35 chars =  280 Pixels
// H Back Porch = 1.101; // (usec) = 61 chars =  488 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 13.338; // (msec) = 1672 lines  HT – (1.06xHA)
Ver Addr Time = 12.764; // (msec) = 1600 lines  = 1.86
Ver Blank Start = 12.764; // (msec) = 1600 lines  
Ver Blank Time = 0.574; // (msec) = 72 lines  
Ver Sync Start = 12.788; // (msec) = 1603 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.024; // (msec) = 3 lines  
Ver Sync Time = 0.048; // (msec) = 6 lines  
// V Back Porch = 0.503; // (msec) = 63 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters  
Timing Name = 2560 x 1600 @ 85Hz;  
Hor Pixels = 2560; // Pixels  
Ver Pixels = 1600; // Lines  
Hor Frequency = 142.887; // kHz = 7.0 usec /  line
Ver Frequency = 84.951; // Hz = 11.8 msec /  frame
Pixel Clock = 505.250; // MHz = 2.0 nsec  ± 0.5%
Character Width = 8; // Pixels = 15.8 nsec  
Scan Type = NONINTERLACED; // H Phase =  4.0 %
Hor Sync Polarity = NEGATIVE // HBlank = 27.6% of HTotal  
Ver Sync Polarity = POSITIVE; // VBlank = 4.9% of VTotal  
Hor Total Time = 6.999; // (usec) = 442 chars =  3536 Pixels
Hor Addr Time = 5.067; // (usec) = 320 chars =  2560 Pixels
Hor Blank Start = 5.067; // (usec) = 320 chars =  2560 Pixels
Hor Blank Time = 1.932; // (usec) = 122 chars =  976 Pixels
Hor Sync Start = 5.478; // (usec) = 346 chars =  2768 Pixels
// H Right Border = 0.000; // (usec) = 0 chars =  0 Pixels
// H Front Porch = 0.412; // (usec) = 26 chars =  208 Pixels
Hor Sync Time = 0.554; // (usec) = 35 chars =  280 Pixels
// H Back Porch = 0.966; // (usec) = 61 chars =  488 Pixels
// H Left Border = 0.000; // (usec) = 0 chars =  0 Pixels
Ver Total Time = 11.772; // (msec) = 1682 lines  HT – (1.06xHA)
Ver Addr Time = 11.198; // (msec) = 1600 lines  = 1.63
Ver Blank Start = 11.198; // (msec) = 1600 lines  
Ver Blank Time = 0.574; // (msec) = 82 lines  
Ver Sync Start = 11.219; // (msec) = 1603 lines  
// V Bottom Border = 0.000; // (msec) = 0 lines  
// V Front Porch = 0.021; // (msec) = 3 lines  
Ver Sync Time = 0.042; // (msec) = 6 lines  
// V Back Porch = 0.511; // (msec) = 73 lines  
// V Top Border = 0.000; // (msec) = 0 lines  
  Detailed Timing Parameters
Timing Name = 2560 x 1600 @ 120Hz CVT (Reduced Blanking);
Hor Pixels = 2560; // Pixels
Ver Pixels = 1600; // Lines
Hor Frequency = 203.217; // kHz = 4.9 usec / line
Ver Frequency = 119.963; // Hz = 8.3 msec / frame
Pixel Clock = 552.750; // MHz = 1.8 nsec ± 0.5%
Character Width = 8; // Pixels = 14.5 nsec
Scan Type = NONINTERLACED; // H Phase = 0.6 %
Hor Sync Polarity = POSITIVE; // HBlank = 5.9% of HTotal
Ver Sync Polarity = NEGATIVE // VBlank = 5.5% of VTotal
Hor Total Time = 4.921; // (usec) = 340 chars = 2720 Pixels
Hor Addr Time = 4.631; // (usec) = 320 chars = 2560 Pixels
Hor Blank Start = 4.631; // (usec) = 320 chars = 2560 Pixels
Hor Blank Time = 0.289; // (usec) = 20 chars = 160 Pixels
Hor Sync Start = 4.718; // (usec) = 326 chars = 2608 Pixels
// H Right Border = 0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch = 0.087; // (usec) = 6 chars = 48 Pixels
Hor Sync Time = 0.058; // (usec) = 4 chars = 32 Pixels
// H Back Porch = 0.145; // (usec) = 10 chars = 80 Pixels
// H Left Border = 0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time = 8.336; // (msec) = 1694 lines HT – (1.06xHA)
Ver Addr Time = 7.873; // (msec) = 1600 lines = 0.01
Ver Blank Start = 7.873; // (msec) = 1600 lines
Ver Blank Time = 0.463; // (msec) = 94 lines
Ver Sync Start = 7.888; // (msec) = 1603 lines
// V Bottom Border = 0.000; // (msec) = 0 lines
// V Front Porch = 0.015; // (msec) = 3 lines
Ver Sync Time = 0.030; // (msec) = 6 lines
// V Back Porch = 0.418; // (msec) = 85 lines
// V Top Border = 0.000; // (msec) = 0 lines
  Detailed Timing Parameters
Timing Name =  4096 x 2160 @ 60Hz CVT (Reduced Blanking v2);
Hor Pixels =  4096; // Pixels
Ver Pixels =  2160; // Lines
Hor Frequency =  133.320; // kHz = 7.5 usec / line
Ver Frequency =  60.000; // Hz = 16.7 msec / frame
Pixel Clock =  556.744; // MHz = 1.8 nsec ± 0.5%
Character Width =  1; // Pixels = 1.8 nsec
Scan Type =  NONINTERLACED; // H Phase = 0.4 %
Hor Sync Polarity =  POSITIVE; // HBlank = 1.9% of HTotal
Ver Sync Polarity =  NEGATIVE // VBlank = 2.8% of VTotal
Hor Total Time =  7.501; // (usec) = 4176 chars = 4176 Pixels
Hor Addr Time =  7.357; // (usec) = 4096 chars = 4096 Pixels
Hor Blank Start =  7.357; // (usec) = 4096 chars = 4096 Pixels
Hor Blank Time =  0.144; // (usec) = 80 chars = 80 Pixels
Hor Sync Start =  7.371; // (usec) = 4104 chars = 4104 Pixels
// H Right Border =  0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch =  0.014; // (usec) = 8 chars = 8 Pixels
Hor Sync Time =  0.057; // (usec) = 32 chars = 32 Pixels
// H Back Porch =  0.072; // (usec) = 40 chars = 40 Pixels
// H Left Border =  0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time =  16.667; // (msec) = 2222 lines HT – (1.06xHA)
Ver Addr Time =  16.202; // (msec) = 2160 lines = -0.3
Ver Blank Start =  16.202; // (msec) = 2160 lines
Ver Blank Time =  0.465; // (msec) = 62 lines
Ver Sync Start =  16.562; // (msec) = 2208 lines
// V Bottom Border =  0.000; // (msec) = 0 lines
// V Front Porch =  0.360; // (msec) = 48 lines
Ver Sync Time =  0.060; // (msec) = 8 lines
// V Back Porch =  0.045; // (msec) = 6 lines
// V Top Border =  0.000; // (msec) = 0 lines
  Detailed Timing Parameters
Timing Name =  4096 x 2160 @ 59.94 Hz CVT (Reduced Blanking v2);
Hor Pixels =  4096; // Pixels
Ver Pixels =  2160; // Lines
Hor Frequency =  133.187; // kHz = 7.5 usec / line
Ver Frequency =  59.940; // Hz = 16.7 msec / frame
Pixel Clock =  556.188; // MHz = 1.8 nsec ± 0.5%
Character Width =  1; // Pixels = 1.8 nsec
Scan Type =  NONINTERLACED; // H Phase = 0.4 %
Hor Sync Polarity =  POSITIVE; // HBlank = 1.9% of HTotal
Ver Sync Polarity =  NEGATIVE // VBlank = 2.8% of VTotal
Hor Total Time =  7.508; // (usec) = 4176 chars = 4176 Pixels
Hor Addr Time =  7.364; // (usec) = 4096 chars = 4096 Pixels
Hor Blank Start =  7.364; // (usec) = 4096 chars = 4096 Pixels
Hor Blank Time =  0.144; // (usec) = 80 chars = 80 Pixels
Hor Sync Start =  7.379; // (usec) = 4104 chars = 4104 Pixels
// H Right Border =  0.000; // (usec) = 0 chars = 0 Pixels
// H Front Porch =  0.014; // (usec) = 8 chars = 8 Pixels
Hor Sync Time =  0.058; // (usec) = 32 chars = 32 Pixels
// H Back Porch =  0.072; // (usec) = 40 chars = 40 Pixels
// H Left Border =  0.000; // (usec) = 0 chars = 0 Pixels
Ver Total Time =  16.683; // (msec) = 2222 lines HT – (1.06xHA)
Ver Addr Time =  16.218; // (msec) = 2160 lines = -0.3
Ver Blank Start =  16.218; // (msec) = 2160 lines
Ver Blank Time =  0.466; // (msec) = 62 lines
Ver Sync Start =  16.578; // (msec) = 2208 lines
// V Bottom Border =  0.000; // (msec) = 0 lines
// V Front Porch =  0.360; // (msec) = 48 lines
Ver Sync Time =  0.060; // (msec) = 8 lines
// V Back Porch =  0.045; // (msec) = 6 lines
// V Top Border =  0.000; // (msec) = 0 lines
"""


@dataclasses.dataclass
class DisplayMode:
    """Combined mode data, parallel to C++ DisplayMode (display_output.h)."""

    size: typing.Tuple[int, int] = (0, 0)
    scan_size: typing.Tuple[int, int] = (0, 0)
    sync_start: typing.Tuple[int, int] = (0, 0)
    sync_end: typing.Tuple[int, int] = (0, 0)
    sync_polarity: typing.Tuple[int, int] = (0, 0)
    doubling: typing.Tuple[int, int] = (0, 0)
    aspect: typing.Tuple[int, int] = (0, 0)
    pixel_khz: int = 0
    nominal_hz: int = 0


cta_vic_mode = {}

for line in CTA_861_TABLE_1.split("\n"):
    line = line.strip()
    if not line: continue

    # Example: 60,65 1280 720 Prog 3300 2020 750 30 18.000 24.0003 59.400
    fields = line.split()
    for vic in fields[0].split(","):
        vic = int(vic)
        mode = cta_vic_mode.setdefault(vic, DisplayMode())
        mode.size = (int(fields[1]), int(fields[2]))
        mode.scan_size = (int(fields[4]), int(fields[6]))
        mode.doubling = (0, {"Prog": 0, "Int": -1}[fields[3]])
        mode.pixel_khz = int(float(fields[10]) * 1e3)
        mode.nominal_hz = int(round(float(fields[9])))

        i_double = 2 ** -mode.doubling[1]
        assert mode.scan_size[0] == mode.size[0] + int(fields[5])
        assert mode.scan_size[1] == mode.size[1] + float(fields[7]) * i_double

        assert abs(mode.pixel_khz / mode.scan_size[0] - float(fields[8])) < 1e-3
        assert abs(
            mode.pixel_khz * 1e3 / mode.scan_size[0] / mode.scan_size[1] -
            float(fields[9]) / i_double
        ) < 1e-2

for line in CTA_861_TABLE_2.split("\n"):
    line = line.strip()
    if not line: continue

    # Example: 60,65 2 1760 40 220 P 5 5 20 P 1 SMPTE 296M [61] 1,2,25
    fields = line.split()
    for vic in fields[0].split(","):
        vic = int(vic)
        mode = cta_vic_mode[vic]

        i_double = 2 ** -mode.doubling[1]
        mode.sync_start = (
            mode.size[0] + int(fields[2]),
            mode.size[1] + int(fields[6]) * i_double
        )
        mode.sync_end = (
            mode.sync_start[0] + int(fields[3]),
            mode.sync_start[1] + int(fields[7]) * i_double
        )
        mode.sync_polarity = (
            {"P": +1, "N": -1}[fields[5]],
            {"P": +1, "N": -1}[fields[9]]
        )

        assert mode.scan_size[0] == mode.sync_end[0] + int(fields[4])
        assert 0 <= (
            mode.scan_size[1] - (mode.sync_end[1] + int(fields[8]) * i_double)
        ) <= 2

for line in CTA_861_TABLE_3.split("\n"):
    line = line.strip()
    if not line: continue

    # Example: 1 640x480p 59.94Hz/60Hz 4:3 1:1
    fields = line.split()
    if fields[1] in ("Forbidden", "Reserved", "No"): continue
    vic = int(fields[0])
    mode = cta_vic_mode[vic]

    aspect_h, aspect_v = fields[3].split(":")
    mode.aspect = (int(aspect_h), int(aspect_v))

    name = fields[1]
    assert name[-1:] == ("i" if mode.doubling[1] < 0 else "p")
    name_h, name_v = name[:-1].split("x")
    if "(" in name_h:
        name_h, name_x = name_h.split("(")
        name_x = name_x.strip(")")
    else:
        name_x = name_h

    name_x, name_h, name_v = int(name_x), int(name_h), int(name_v)
    assert (name_x, name_v) == mode.size
    if name_x != name_h:
        assert name_x == name_h * 2
        mode.size = (name_h, name_v)
        mode.doubling = (1, mode.doubling[1])
        mode.pixel_khz = mode.pixel_khz // 2
        for field in ("scan_size", "sync_start", "sync_end"):
            x, y = getattr(mode, field)
            setattr(mode, field, (x // 2, y))

    pix_field = " ".join(fields[4:])
    if " " not in pix_field and "-" not in pix_field and "," not in pix_field:
        pixel_h, pixel_v = pix_field.split(":")
        pixel_ratio = int(pixel_h) / int(pixel_v)
        exp = (mode.aspect[0] / mode.size[0]) / (mode.aspect[1] / mode.size[1])
        assert abs(pixel_ratio - exp) < 1e-3


vesa_dmt_modes = []
for block in VESA_DMT_TABLE.split("Detailed Timing Parameters"):
    v = {}
    for line in block.split("\n"):
        if line.isspace() or not line: continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.split("//")[0].strip().strip(";")
        v[key] = value

    if not v: continue

    mode = DisplayMode()
    mode.size = (int(v["Hor Pixels"]), int(v["Ver Pixels"]))
    mode.nominal_hz = round(float(v["Ver Frequency"]))
    mode.pixel_khz = int(float(v["Pixel Clock"]) * 1e3)
    pix_usec = 1e3 / mode.pixel_khz
    line_msec = float(v["Hor Total Time"]) * 1e-3

    mode.scan_size = (
        round(float(v["Hor Total Time"]) / pix_usec),
        round(float(v["Ver Total Time"]) / line_msec)
    )
    mode.sync_start = (
        round(float(v["Hor Sync Start"]) / pix_usec),
        round(float(v["Ver Sync Start"]) / line_msec)
    )
    mode.sync_end = (
        mode.sync_start[0] + round(float(v["Hor Sync Time"]) / pix_usec),
        mode.sync_start[1] + round(float(v["Ver Sync Time"]) / line_msec)
    )
    mode.sync_polarity = (
        {"NEGATIVE": -1, "POSITIVE": +1}[v["Hor Sync Polarity"]],
        {"NEGATIVE": -1, "POSITIVE": +1}[v["Ver Sync Polarity"]]
    )
    mode.sync_doubling = (
        0, {"NONINTERLACED": 0, "INTERLACED": -1}[v["Scan Type"]]
    )

    assert abs(float(v["Hor Addr Time"]) - mode.size[0] * pix_usec < 1)
    assert abs(float(v["Ver Addr Time"]) - mode.size[1] * line_msec < 1)
    vesa_dmt_modes.append(mode)


parser = argparse.ArgumentParser()
parser.add_argument("--output", help="File to generate")
args = parser.parse_args()


output = open(args.output, "w") if args.output else sys.stdout

for table, modes in [
    ("cta_861_modes", [cta_vic_mode[v] for v in sorted(cta_vic_mode.keys())]),
    ("vesa_dmt_modes", vesa_dmt_modes)
]:
    print(f"std::vector<pivid::DisplayMode> const {table} = {{", file=output)
    for m in modes:
        xy = lambda xy: f"{{{xy[0]}, {xy[1]}}}"
        print(
            "  {"
            f".size={xy(m.size)}, "
            f".scan_size={xy(m.scan_size)}, "
            f".sync_start={xy(m.sync_start)}, "
            f".sync_end={xy(m.sync_end)}, "
            f".sync_polarity={xy(m.sync_polarity)}, "
            f".doubling={xy(m.doubling)}, "
            f".aspect={xy(m.aspect)}, "
            f".pixel_khz={m.pixel_khz}, "
            f".nominal_hz={m.nominal_hz}, "
            "},",
            file=output
        )

    print("};\n", file=output)
