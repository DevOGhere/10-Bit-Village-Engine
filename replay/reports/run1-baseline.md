# 10-Bit Village — Run Analysis: run1-baseline

Source: `/private/tmp/claude-501/-Users-ojaswi-Desktop-Projects-Obsidian-OjiLLMbrains/71d279bc-b579-403f-8c56-6c3b12c05bfd/scratchpad/run1_dev/village.db`  
Tick range: 0–31781 (32 buckets of 1000 ticks)  
CognitionLog rows analyzed: 31782

## 1–3, 7. Per-bucket time series

| bucket | tick_start | voice_convergence | opener_concentration | vocab_diversity(trigram) | truncation_rate |
|---|---|---|---|---|---|
| 0 | 0 | 0.4925 | 0.0760 | 0.7645 | 0.8830 |
| 1 | 1000 | 0.4958 | 0.0780 | 0.7544 | 0.8890 |
| 2 | 2000 | 0.5441 | 0.0610 | 0.7071 | 0.8990 |
| 3 | 3000 | 0.5496 | 0.0680 | 0.6869 | 0.9200 |
| 4 | 4000 | 0.4772 | 0.0640 | 0.7365 | 0.8910 |
| 5 | 5000 | 0.4716 | 0.0770 | 0.7450 | 0.9080 |
| 6 | 6000 | 0.4717 | 0.0670 | 0.7370 | 0.9190 |
| 7 | 7000 | 0.4881 | 0.0700 | 0.7300 | 0.8890 |
| 8 | 8000 | 0.5325 | 0.0660 | 0.6992 | 0.8940 |
| 9 | 9000 | 0.5485 | 0.0650 | 0.6779 | 0.9170 |
| 10 | 10000 | 0.5494 | 0.0740 | 0.6818 | 0.8960 |
| 11 | 11000 | 0.5550 | 0.0670 | 0.6759 | 0.8910 |
| 12 | 12000 | 0.5472 | 0.0780 | 0.6750 | 0.9060 |
| 13 | 13000 | 0.5591 | 0.0600 | 0.6732 | 0.9010 |
| 14 | 14000 | 0.5702 | 0.0630 | 0.6694 | 0.8990 |
| 15 | 15000 | 0.5408 | 0.0710 | 0.6801 | 0.8930 |
| 16 | 16000 | 0.5520 | 0.0610 | 0.6844 | 0.9110 |
| 17 | 17000 | 0.5465 | 0.0630 | 0.6835 | 0.9070 |
| 18 | 18000 | 0.5631 | 0.0620 | 0.6811 | 0.9130 |
| 19 | 19000 | 0.5440 | 0.0520 | 0.6852 | 0.9060 |
| 20 | 20000 | 0.5554 | 0.0650 | 0.6782 | 0.8910 |
| 21 | 21000 | 0.5549 | 0.0700 | 0.6737 | 0.9050 |
| 22 | 22000 | 0.5567 | 0.0570 | 0.6719 | 0.8840 |
| 23 | 23000 | 0.5448 | 0.0640 | 0.6736 | 0.8890 |
| 24 | 24000 | 0.5448 | 0.0620 | 0.6700 | 0.9150 |
| 25 | 25000 | 0.5526 | 0.0580 | 0.6810 | 0.8980 |
| 26 | 26000 | 0.5429 | 0.0690 | 0.6826 | 0.9040 |
| 27 | 27000 | 0.5542 | 0.0660 | 0.6750 | 0.9000 |
| 28 | 28000 | 0.5472 | 0.0780 | 0.6834 | 0.8970 |
| 29 | 29000 | 0.5381 | 0.0670 | 0.6839 | 0.9040 |
| 30 | 30000 | 0.5486 | 0.0680 | 0.6733 | 0.9020 |
| 31 | 31000 | 0.4580 | 0.0537 | 0.7140 | 0.8951 |

## 4. Lineage demography (HearsayChain)

- HearsayChain rows: 27070
- Multi-hop lineages: 1710
- Lineage lifetime (ticks) — mean: 834.0, median: 712.0, max: 5100
- Hop-depth histogram: {1: 10029, 2: 14631, 3: 2398, 4: 12}

| bucket | active_lineages | mean_hop_depth |
|---|---|---|
| 0 | 77 | 1.32 |
| 1 | 113 | 1.69 |
| 2 | 119 | 1.92 |
| 3 | 128 | 1.92 |
| 4 | 88 | 1.79 |
| 5 | 94 | 1.61 |
| 6 | 89 | 1.62 |
| 7 | 90 | 1.66 |
| 8 | 95 | 1.68 |
| 9 | 104 | 1.75 |
| 10 | 105 | 1.78 |
| 11 | 96 | 1.77 |
| 12 | 103 | 1.73 |
| 13 | 112 | 1.71 |
| 14 | 102 | 1.68 |
| 15 | 101 | 1.77 |
| 16 | 108 | 1.68 |
| 17 | 104 | 1.65 |
| 18 | 110 | 1.68 |
| 19 | 93 | 1.76 |
| 20 | 107 | 1.67 |
| 21 | 101 | 1.75 |
| 22 | 99 | 1.73 |
| 23 | 100 | 1.76 |
| 24 | 95 | 1.70 |
| 25 | 99 | 1.70 |
| 26 | 106 | 1.71 |
| 27 | 96 | 1.77 |
| 28 | 91 | 1.68 |
| 29 | 103 | 1.81 |
| 30 | 104 | 1.76 |
| 31 | 93 | 1.72 |

## 5. Mutation vs genome (H1: high-suspicion mutates more)

| suspicion (0-3) | mean content_word_delta | n |
|---|---|---|
| 0 | 33.33 | 4080 |
| 1 | 33.64 | 7554 |
| 2 | 33.75 | 7847 |
| 3 | 33.57 | 7589 |

| curiosity (0-3) | mean content_word_delta | n |
|---|---|---|
| 0 | 33.58 | 8664 |
| 1 | 33.67 | 6748 |
| 2 | 33.54 | 5685 |
| 3 | 33.64 | 5973 |

## 6. Coinage adoption (H3: adoption follows grid proximity)

- Total adoption events: 68609
- Adoptions with position data (VillagerState retention window): 3366
- Pearson r (grid distance vs adoption lag), n=3366: 0.0592
- Note: position coverage limited to VillagerState's retained tick window (pruned in production; see B... pruning). r is computed only over adoptions whose tick fell inside that window.

Top-15 terms by adopter count — cumulative distinct-adopter curve (tick, cum_adopters):

- **allowing**: [(320, 1), (2038, 13), (3208, 25), (3632, 37), (6657, 49), (8471, 61), (10688, 73), (17499, 85), (24751, 97)]
- **appears**: [(39, 1), (332, 13), (1048, 25), (1528, 37), (1924, 49), (2737, 61), (3175, 73), (4569, 85), (8187, 97)]
- **awakened**: [(1248, 1), (2312, 13), (3390, 25), (4058, 37), (5732, 49), (7657, 61), (9714, 73), (13872, 85), (20182, 97)]
- **begins**: [(128, 1), (378, 13), (681, 25), (1060, 37), (1421, 49), (1934, 61), (2450, 73), (2736, 85), (5482, 97)]
- **boundaries**: [(435, 1), (2038, 13), (2543, 25), (3093, 37), (3455, 49), (4775, 61), (6652, 73), (9007, 85), (14666, 97)]
- **deeper**: [(133, 1), (1021, 13), (1664, 25), (2045, 37), (2354, 49), (2830, 61), (3462, 73), (4932, 85), (9413, 97)]
- **defies**: [(256, 1), (1789, 13), (2793, 25), (3355, 37), (4211, 49), (6270, 61), (8413, 73), (11125, 85), (16245, 97)]
- **depths**: [(171, 1), (1559, 13), (2320, 25), (2763, 37), (4118, 49), (6956, 61), (8666, 73), (10904, 85), (15297, 97)]
- **desires**: [(62, 1), (1568, 13), (2184, 25), (2906, 37), (4708, 49), (7503, 61), (10877, 73), (13659, 85), (24372, 97)]
- **disorienting**: [(358, 1), (2296, 13), (3049, 25), (4489, 37), (6943, 49), (8099, 61), (8746, 73), (10470, 85), (14985, 97)]
- **doesn**: [(33, 1), (199, 13), (506, 25), (876, 37), (1224, 49), (1602, 61), (3657, 73), (6425, 85), (14465, 97)]
- **dreams**: [(118, 1), (966, 13), (1673, 25), (2026, 37), (2200, 49), (2505, 61), (3208, 73), (4168, 85), (8767, 97)]
- **dreamscape**: [(186, 1), (537, 13), (1153, 25), (1640, 37), (2096, 49), (2614, 61), (3209, 73), (3957, 85), (12166, 97)]
- **ears**: [(40, 1), (905, 13), (2392, 25), (3818, 37), (4456, 49), (5937, 61), (9354, 73), (13977, 85), (25360, 97)]
- **echoes**: [(179, 1), (1108, 13), (1712, 25), (1935, 37), (2140, 49), (2511, 61), (2913, 73), (3799, 85), (7747, 97)]

## 8. Myth-persistence (calcification guardrail, §0.1)

- Raw CoinedWords: 5272 terms, 913 survive a B5-style inflection/fragment filter (0.827 noise rate) -- **matches packet 098's ~90% noise estimate.**
- Calcified-motif count on RAW CoinedWords (>=3 villagers AND >=5000-tick lifetime): 2862 -- **inflated by inflection noise, not a clean number.**
- Calcified-motif count on FILTERED terms (post B5-style filter) -- **this is the number Run 2 should be compared against**: **347**
- Filtered qualifying terms: ['thronglet', 'heard', 'shouldn', 'memories', 'stories', 'thronglets', 'elara', 'flies', 'supplies', 'cooperative', 'enemies', 'festivities', 'bellies', 'relied', 'doesn', 'possibilities', 'tendencies', 'finnley', 'thorne', 'accompanied', 'began', 'women', 'anymore', 'cooperation', 'proud', 'marketplace', 'luxuries', 'expertise', 'opportunities', 'bookshelves', 'abilities', 'families', 'communities', 'overheard', 'difficulties', 'heartwarming', 'rooftops', 'wouldn', 'mysteries', 'worries', 'theories', 'defies', 'dreamscape', 'couldn', 'implies', 'boundaries', 'surreal', 'birdsong', 'simplest', 'amplified', 'kaelin', 'earlier', 'soundtrack', 'wildflowers', 'realities', 'unfazed', 'weren', 'became', 'mystique', 'greatest', 'pleasantries', 'cities', 'finest', 'pastries', 'authorities', 'bodies', 'timeline', 'twenties', 'ravenswood', 'complexities', 'rivalries', 'activities', 'watercolors', 'peculiarities', 'deepest', 'melodies', 'fireflies', 'newfound', 'necessities', 'intricacies', 'eccentricities', 'energies', 'oddities', 'brindlemark', 'selves', 'ponytail', 'harmonies', 'anxieties', 'sensibilities', 'gemstone', 'territories', 'backstory', 'rewritten', 'inconsistencies', 'plato', 'judgmental', 'uncertainties', 'unpredictability', 'confetti', 'justified', 'entities', 'properties', 'passersby', 'dreamscapes', 'similarities', 'studies', 'terrified', 'interconnectedness', 'wildest', 'dreamspace', 'embodies', 'jigsaw', 'awestruck', 'heavier', 'grimbold', 'prophecies', 'breathtaking', 'darkest', 'fishermen', 'identities', 'ravenshire', 'identified', 'breathtakingly', 'windswept', 'disembodied', 'defied', 'gemstones', 'rubies', 'centuries', 'soundscape', 'subtlest', 'tapestries', 'discoveries', 'undergone', 'masterclass', 'brushstrokes', 'anomalies', 'histories', 'reconfigured', 'floorboards', 'reevaluate', 'artwork', 'occupied', 'deities', 'faintest', 'woken', 'dreamwalker', 'mythologies', 'societies', 'firestorm', 'timelines', 'groundbreaking', 'mythic', 'subtleties', 'stardust', 'ceremonies', 'responsibilities', 'mesmerizingly', 'personalities', 'cries', 'awoken', 'hometown', 'ninear', 'hallucinogen', 'pushy', 'treeline', 'spies', 'watercolor', 'accompanies', 'gryffindor', 'hogwarts', 'intensifies', 'thinnest', 'frequencies', 'propels', 'gotta', 'eryndor', 'closest', 'shapeshifting', 'hardworking', 'beneathfoot', 'ambiance', 'thoren', 'intensified', 'libraries', 'woodsmoke', 'breadcrumbs', 'denied', 'craftsmen', 'reenacting', 'categories', 'parties', 'canopies', 'geometries', 'epiphanies', 'qualities', 'soundscapes', 'lunchtime', 'dreamweaver', 'elyria', 'aethera', 'sentin', 'signifies', 'whimsy', 'insecurities', 'salvador', 'einar', 'akira', 'reconfiguring', 'dreamwalking', 'inexplic', 'unnervingly', 'takeaway', 'goosebumps', 'miscommunication', 'potentialities', 'riverbed', 'axhalt', 'storylines', 'fancies', 'starl', 'silvermist', 'earliest', 'thorold', 'shadowborn', 'brushstroke', 'wildflower', 'recol', 'slightest', 'sandcastles', 'shimm', 'wisest', 'lilies', 'coastline', 'fractal', 'luminaria', 'firehose', 'eldrida', 'luminari', 'technologies', 'kanaq', 'minefield', 'symmetries', 'folktale', 'gooseflesh', 'purest', 'cauldron', 'interdimensional', 'lullabies', 'destinies', 'threadwork', 'hubris', 'tragedies', 'leanin', 'boxes', 'erebus', 'swirlin', 'softest', 'saltwater', 'hauntings', 'aethoria', 'greece', 'smallest', 'comms', 'amplifies', 'shadowwood', 'trivialities', 'mimicking', 'fractals', 'gorvoth', 'applies', 'magnified', 'reconfigures', 'kaleid', 'relies', 'kairos', 'flashbacks', 'mindscape', 'seagulls', 'capabilities', 'foreb', 'synchronicity', 'dreamwork', 'visionaries', 'reconfigure', 'fairies', 'vulnerabilities', 'spacetime', 'versa', 'blurriness', 'inextric', 'eldrador', 'chamomile', 'elwynn', 'oldest', 'downplay', 'aviemore', 'seashells', 'galaxies', 'silhou', 'longhouse', 'claustrophobic', 'dreamsscape', 'ashwood', 'remedies', 'dreamsong', 'elyrian', 'skyline', 'imaginings', 'cryptozoologist', 'unsett', 'manif', 'shockwaves', 'reexamine', 'lashings', 'willpower', 'worldview', 'rooftop', 'nightdream', 'appar', 'shrou', 'whisperiness', 'clich', 'impactful', 'intox', 'dreamwalkers', 'mindsets', 'butterflies', 'jungian', 'talkin', 'lookin', 'somethin', 'zorvath', 'faculties', 'windswe', 'malle', 'probabilities', 'reevaluated', 'mindset', 'coastlines', 'befallen', 'cacoph', 'escalation', 'landmass', 'orinoco', 'gudrun', 'replayable']

| bucket | cumulative_calcified_motifs_raw | cumulative_calcified_motifs_filtered |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 0 | 0 |
| 2 | 0 | 0 |
| 3 | 0 | 0 |
| 4 | 0 | 0 |
| 5 | 1062 | 90 |
| 6 | 1404 | 130 |
| 7 | 1587 | 150 |
| 8 | 1705 | 167 |
| 9 | 1808 | 187 |
| 10 | 1894 | 197 |
| 11 | 1972 | 206 |
| 12 | 2034 | 218 |
| 13 | 2100 | 225 |
| 14 | 2161 | 234 |
| 15 | 2216 | 241 |
| 16 | 2269 | 249 |
| 17 | 2314 | 256 |
| 18 | 2366 | 261 |
| 19 | 2427 | 268 |
| 20 | 2460 | 276 |
| 21 | 2498 | 286 |
| 22 | 2533 | 291 |
| 23 | 2577 | 296 |
| 24 | 2611 | 301 |
| 25 | 2647 | 304 |
| 26 | 2689 | 311 |
| 27 | 2716 | 313 |
| 28 | 2753 | 322 |
| 29 | 2790 | 334 |
| 30 | 2824 | 338 |
| 31 | 2852 | 344 |
