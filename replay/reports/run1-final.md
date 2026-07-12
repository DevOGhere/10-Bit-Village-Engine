# 10-Bit Village — Run Analysis: run1-final

Source: `/tmp/tbv_cutover/village.db`  
Tick range: 0–69702 (70 buckets of 1000 ticks)  
CognitionLog rows analyzed: 69703

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
| 31 | 31000 | 0.5331 | 0.0520 | 0.6893 | 0.8940 |
| 32 | 32000 | 0.5400 | 0.0690 | 0.6829 | 0.9110 |
| 33 | 33000 | 0.5564 | 0.0550 | 0.6893 | 0.9190 |
| 34 | 34000 | 0.5460 | 0.0640 | 0.6803 | 0.8990 |
| 35 | 35000 | 0.5435 | 0.0620 | 0.6727 | 0.8900 |
| 36 | 36000 | 0.5462 | 0.0630 | 0.6734 | 0.8860 |
| 37 | 37000 | 0.5404 | 0.0680 | 0.6794 | 0.8890 |
| 38 | 38000 | 0.5508 | 0.0570 | 0.6826 | 0.8920 |
| 39 | 39000 | 0.5528 | 0.0670 | 0.6793 | 0.9080 |
| 40 | 40000 | 0.5548 | 0.0690 | 0.6719 | 0.9050 |
| 41 | 41000 | 0.5572 | 0.0740 | 0.6843 | 0.9000 |
| 42 | 42000 | 0.5472 | 0.0680 | 0.6754 | 0.8790 |
| 43 | 43000 | 0.5381 | 0.0670 | 0.6819 | 0.8900 |
| 44 | 44000 | 0.5481 | 0.0590 | 0.6804 | 0.8910 |
| 45 | 45000 | 0.5455 | 0.0750 | 0.6698 | 0.9000 |
| 46 | 46000 | 0.5536 | 0.0640 | 0.6833 | 0.9120 |
| 47 | 47000 | 0.5577 | 0.0700 | 0.6725 | 0.8990 |
| 48 | 48000 | 0.5543 | 0.0550 | 0.6848 | 0.9030 |
| 49 | 49000 | 0.5422 | 0.0680 | 0.6871 | 0.8910 |
| 50 | 50000 | 0.5558 | 0.0600 | 0.6895 | 0.8860 |
| 51 | 51000 | 0.5363 | 0.0670 | 0.6867 | 0.9080 |
| 52 | 52000 | 0.5384 | 0.0730 | 0.6737 | 0.8950 |
| 53 | 53000 | 0.5458 | 0.0550 | 0.6849 | 0.8930 |
| 54 | 54000 | 0.5367 | 0.0720 | 0.6941 | 0.8980 |
| 55 | 55000 | 0.5427 | 0.0620 | 0.6868 | 0.9130 |
| 56 | 56000 | 0.5456 | 0.0700 | 0.6816 | 0.8850 |
| 57 | 57000 | 0.5447 | 0.0610 | 0.6929 | 0.9130 |
| 58 | 58000 | 0.5402 | 0.0670 | 0.6850 | 0.9040 |
| 59 | 59000 | 0.5463 | 0.0700 | 0.6835 | 0.8950 |
| 60 | 60000 | 0.5367 | 0.0580 | 0.6838 | 0.9090 |
| 61 | 61000 | 0.5444 | 0.0670 | 0.6779 | 0.9020 |
| 62 | 62000 | 0.5453 | 0.0660 | 0.6856 | 0.8980 |
| 63 | 63000 | 0.5354 | 0.0630 | 0.6825 | 0.9040 |
| 64 | 64000 | 0.5430 | 0.0530 | 0.6763 | 0.9080 |
| 65 | 65000 | 0.5422 | 0.0710 | 0.6737 | 0.8810 |
| 66 | 66000 | 0.5439 | 0.0710 | 0.6785 | 0.8980 |
| 67 | 67000 | 0.5510 | 0.0670 | 0.6822 | 0.8990 |
| 68 | 68000 | 0.5352 | 0.0520 | 0.6920 | 0.9020 |
| 69 | 69000 | 0.4368 | 0.0697 | 0.7208 | 0.8862 |

## 4. Lineage demography (HearsayChain)

- HearsayChain rows: 59690
- Multi-hop lineages: 3677
- Lineage lifetime (ticks) — mean: 855.5, median: 768, max: 5100
- Hop-depth histogram: {1: 21533, 2: 32680, 3: 5465, 4: 12}

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
| 31 | 114 | 1.71 |
| 32 | 101 | 1.75 |
| 33 | 115 | 1.68 |
| 34 | 105 | 1.72 |
| 35 | 100 | 1.74 |
| 36 | 102 | 1.67 |
| 37 | 94 | 1.81 |
| 38 | 111 | 1.77 |
| 39 | 113 | 1.76 |
| 40 | 96 | 1.76 |
| 41 | 104 | 1.77 |
| 42 | 89 | 1.71 |
| 43 | 105 | 1.73 |
| 44 | 104 | 1.66 |
| 45 | 92 | 1.72 |
| 46 | 109 | 1.70 |
| 47 | 98 | 1.66 |
| 48 | 98 | 1.72 |
| 49 | 90 | 1.85 |
| 50 | 104 | 1.78 |
| 51 | 90 | 1.79 |
| 52 | 111 | 1.76 |
| 53 | 102 | 1.69 |
| 54 | 94 | 1.74 |
| 55 | 90 | 1.74 |
| 56 | 90 | 1.74 |
| 57 | 100 | 1.76 |
| 58 | 102 | 1.75 |
| 59 | 108 | 1.76 |
| 60 | 102 | 1.72 |
| 61 | 92 | 1.80 |
| 62 | 103 | 1.73 |
| 63 | 104 | 1.81 |
| 64 | 101 | 1.75 |
| 65 | 99 | 1.80 |
| 66 | 107 | 1.69 |
| 67 | 100 | 1.70 |
| 68 | 100 | 1.74 |
| 69 | 88 | 1.77 |

## 5. Mutation vs genome (H1: high-suspicion mutates more)

| suspicion (0-3) | mean content_word_delta | n |
|---|---|---|
| 0 | 33.54 | 8979 |
| 1 | 33.60 | 16682 |
| 2 | 33.63 | 17332 |
| 3 | 33.58 | 16697 |

| curiosity (0-3) | mean content_word_delta | n |
|---|---|---|
| 0 | 33.60 | 19073 |
| 1 | 33.58 | 14881 |
| 2 | 33.56 | 12559 |
| 3 | 33.62 | 13177 |

## 6. Coinage adoption (H3: adoption follows grid proximity)

- Total adoption events: 103149
- Adoptions with position data (VillagerState retention window): 1927
- Pearson r (grid distance vs adoption lag), n=1927: 0.0484
- Note: position coverage limited to VillagerState's retained tick window (pruned in production; see B... pruning). r is computed only over adoptions whose tick fell inside that window.

Top-15 terms by adopter count — cumulative distinct-adopter curve (tick, cum_adopters):

- **allowing**: [(320, 1), (2038, 13), (3208, 25), (3632, 37), (6657, 49), (8471, 61), (10688, 73), (17499, 85), (24751, 97)]
- **answers**: [(53, 1), (1019, 13), (1740, 25), (2943, 37), (4042, 49), (5935, 61), (9250, 73), (12634, 85), (21886, 97)]
- **appears**: [(39, 1), (332, 13), (1048, 25), (1528, 37), (1924, 49), (2737, 61), (3175, 73), (4569, 85), (8187, 97)]
- **aren**: [(30, 1), (1647, 13), (3524, 25), (5913, 37), (7376, 49), (10629, 61), (13097, 73), (19427, 85), (40122, 97)]
- **aspects**: [(1064, 1), (2648, 13), (4389, 25), (7198, 37), (9556, 49), (12184, 61), (16131, 73), (20604, 85), (38199, 97)]
- **attuned**: [(1317, 1), (2971, 13), (3790, 25), (6152, 37), (8214, 49), (10853, 61), (13102, 73), (18234, 85), (28669, 97)]
- **awakened**: [(1248, 1), (2312, 13), (3390, 25), (4058, 37), (5732, 49), (7657, 61), (9714, 73), (13872, 85), (20182, 97)]
- **began**: [(158, 1), (1183, 13), (2045, 25), (3326, 37), (4313, 49), (5761, 61), (7693, 73), (9254, 85), (16359, 97)]
- **begins**: [(128, 1), (378, 13), (681, 25), (1060, 37), (1421, 49), (1934, 61), (2450, 73), (2736, 85), (5482, 97)]
- **blurring**: [(683, 1), (3218, 13), (5619, 25), (8450, 37), (9525, 49), (12590, 61), (15026, 73), (20733, 85), (40871, 97)]
- **boundaries**: [(435, 1), (2038, 13), (2543, 25), (3093, 37), (3455, 49), (4775, 61), (6652, 73), (9007, 85), (14666, 97)]
- **branches**: [(171, 1), (686, 13), (2003, 25), (3358, 37), (4248, 49), (6507, 61), (8327, 73), (13251, 85), (24772, 97)]
- **buildings**: [(129, 1), (1776, 13), (3082, 25), (5300, 37), (7339, 49), (10443, 61), (14219, 73), (21268, 85), (46674, 97)]
- **centuries**: [(2014, 1), (3503, 13), (5501, 25), (8664, 37), (11257, 49), (14199, 61), (16170, 73), (21055, 85), (32427, 97)]
- **challenges**: [(122, 1), (2088, 13), (3784, 25), (7393, 37), (9439, 49), (11782, 61), (20064, 73), (27673, 85), (51954, 97)]

## 8. Myth-persistence (calcification guardrail, §0.1)

- Raw CoinedWords: 6662 terms, 1342 survive a B5-style inflection/fragment filter (0.799 noise rate) -- **matches packet 098's ~90% noise estimate.**
- Calcified-motif count on RAW CoinedWords (>=3 villagers AND >=5000-tick lifetime): 3767 -- **inflated by inflection noise, not a clean number.**
- Calcified-motif count on FILTERED terms (post B5-style filter) -- **this is the number Run 2 should be compared against**: **496**
- Filtered qualifying terms: ['thronglet', 'heard', 'shouldn', 'memories', 'stories', 'thronglets', 'elara', 'flies', 'supplies', 'cooperative', 'enemies', 'festivities', 'bellies', 'relied', 'doesn', 'possibilities', 'tendencies', 'finnley', 'thorne', 'accompanied', 'began', 'women', 'anymore', 'cooperation', 'proud', 'marketplace', 'luxuries', 'expertise', 'opportunities', 'bookshelves', 'abilities', 'families', 'communities', 'overheard', 'difficulties', 'heartwarming', 'rooftops', 'wouldn', 'mysteries', 'worries', 'theories', 'defies', 'dreamscape', 'couldn', 'implies', 'boundaries', 'surreal', 'birdsong', 'simplest', 'amplified', 'kaelin', 'earlier', 'soundtrack', 'wildflowers', 'realities', 'unfazed', 'weren', 'became', 'mystique', 'greatest', 'pleasantries', 'cities', 'finest', 'pastries', 'authorities', 'bodies', 'timeline', 'twenties', 'ravenswood', 'complexities', 'rivalries', 'activities', 'watercolors', 'peculiarities', 'deepest', 'melodies', 'fireflies', 'newfound', 'necessities', 'intricacies', 'eccentricities', 'energies', 'oddities', 'brindlemark', 'selves', 'ponytail', 'harmonies', 'anxieties', 'sensibilities', 'gemstone', 'territories', 'backstory', 'rewritten', 'inconsistencies', 'plato', 'judgmental', 'uncertainties', 'unpredictability', 'confetti', 'justified', 'entities', 'properties', 'passersby', 'dreamscapes', 'similarities', 'studies', 'terrified', 'interconnectedness', 'wildest', 'dreamspace', 'embodies', 'jigsaw', 'awestruck', 'heavier', 'grimbold', 'prophecies', 'breathtaking', 'darkest', 'fishermen', 'identities', 'ravenshire', 'identified', 'breathtakingly', 'windswept', 'disembodied', 'defied', 'gemstones', 'rubies', 'centuries', 'soundscape', 'subtlest', 'tapestries', 'discoveries', 'undergone', 'masterclass', 'brushstrokes', 'anomalies', 'histories', 'reconfigured', 'floorboards', 'reevaluate', 'artwork', 'occupied', 'deities', 'faintest', 'woken', 'dreamwalker', 'mythologies', 'societies', 'firestorm', 'timelines', 'groundbreaking', 'mythic', 'subtleties', 'stardust', 'ceremonies', 'responsibilities', 'mesmerizingly', 'personalities', 'cries', 'awoken', 'hometown', 'ninear', 'hallucinogen', 'pushy', 'treeline', 'spies', 'watercolor', 'accompanies', 'gryffindor', 'hogwarts', 'intensifies', 'thinnest', 'frequencies', 'propels', 'gotta', 'eryndor', 'closest', 'shapeshifting', 'hardworking', 'beneathfoot', 'ambiance', 'thoren', 'intensified', 'libraries', 'woodsmoke', 'breadcrumbs', 'denied', 'craftsmen', 'reenacting', 'categories', 'parties', 'canopies', 'geometries', 'epiphanies', 'qualities', 'soundscapes', 'lunchtime', 'dreamweaver', 'elyria', 'aethera', 'sentin', 'signifies', 'whimsy', 'insecurities', 'salvador', 'einar', 'akira', 'reconfiguring', 'dreamwalking', 'inexplic', 'unnervingly', 'takeaway', 'goosebumps', 'miscommunication', 'potentialities', 'riverbed', 'axhalt', 'storylines', 'fancies', 'starl', 'silvermist', 'earliest', 'thorold', 'shadowborn', 'brushstroke', 'wildflower', 'recol', 'slightest', 'sandcastles', 'shimm', 'wisest', 'lilies', 'coastline', 'fractal', 'luminaria', 'firehose', 'eldrida', 'luminari', 'technologies', 'kanaq', 'minefield', 'symmetries', 'folktale', 'gooseflesh', 'purest', 'cauldron', 'interdimensional', 'lullabies', 'destinies', 'threadwork', 'hubris', 'tragedies', 'leanin', 'boxes', 'erebus', 'swirlin', 'softest', 'saltwater', 'hauntings', 'aethoria', 'greece', 'smallest', 'comms', 'amplifies', 'shadowwood', 'trivialities', 'mimicking', 'fractals', 'gorvoth', 'applies', 'magnified', 'reconfigures', 'kaleid', 'relies', 'kairos', 'flashbacks', 'mindscape', 'seagulls', 'capabilities', 'foreb', 'synchronicity', 'dreamwork', 'visionaries', 'reconfigure', 'fairies', 'vulnerabilities', 'spacetime', 'versa', 'blurriness', 'inextric', 'eldrador', 'chamomile', 'elwynn', 'oldest', 'downplay', 'aviemore', 'seashells', 'galaxies', 'silhou', 'longhouse', 'claustrophobic', 'dreamsscape', 'ashwood', 'remedies', 'dreamsong', 'elyrian', 'skyline', 'imaginings', 'cryptozoologist', 'unsett', 'manif', 'shockwaves', 'reexamine', 'lashings', 'willpower', 'worldview', 'rooftop', 'nightdream', 'appar', 'shrou', 'whisperiness', 'clich', 'impactful', 'intox', 'dreamwalkers', 'mindsets', 'butterflies', 'jungian', 'talkin', 'lookin', 'somethin', 'zorvath', 'faculties', 'windswe', 'malle', 'probabilities', 'reevaluated', 'mindset', 'coastlines', 'befallen', 'cacoph', 'escalation', 'landmass', 'embodied', 'dreamtime', 'seafloor', 'midair', 'reenactment', 'jawline', 'bravest', 'eldoria', 'perme', 'apologies', 'repositories', 'entries', 'orinoco', 'philosophies', 'gudrun', 'freud', 'replayable', 'windowsill', 'ettins', 'lyraea', 'muddied', 'liminality', 'goroth', 'moonwhisper', 'confrontational', 'wavelengths', 'elwes', 'empathetic', 'duties', 'recontextualized', 'implied', 'dragonfire', 'shadowfell', 'injuries', 'teleportation', 'tanginess', 'phobias', 'willowdale', 'rollercoaster', 'byproduct', 'terra', 'shoreline', 'mustn', 'streetlights', 'neuroscience', 'dragonslayer', 'trepidatious', 'masterstroke', 'allegories', 'reconfiguration', 'swayin', 'commonalities', 'unfocus', 'reenvisioning', 'unseasonal', 'strongest', 'pranksterism', 'obliv', 'tolkien', 'mockingly', 'propelling', 'symphonies', 'kabbalah', 'unconsciousal', 'propel', 'lumin', 'thompson', 'bogglingly', 'dreamkeeper', 'europe', 'liesbeth', 'comin', 'kinda', 'tellin', 'replies', 'directionlessness', 'backyard', 'makeup', 'windowsills', 'echoflux', 'depthlessness', 'atlantis', 'infinitum', 'pedest', 'glitch', 'endgame', 'intertw', 'longest', 'weightier', 'worthwhile', 'empath', 'reenacted', 'streetlight', 'calamities', 'sweetest', 'gorin', 'reson', 'dreamser', 'balconies', 'aethereia', 'multi', 'commodities', 'shadowglow', 'frailties', 'ambiguities', 'taxidermied', 'france', 'eridoria', 'aethon', 'intric', 'befell', 'synchronicities', 'recontextualizing', 'england', 'guardsmen', 'indist', 'cookies', 'marketplaces', 'folkkeeper', 'counterintuitive', 'aetherwood', 'unfocusing', 'propelled', 'willowhaven', 'ghulam', 'silkiest', 'amnesiac', 'london', 'discrepancies', 'thinn', 'tryin', 'notepad', 'ironfist', 'spect', 'mystified', 'lovecraft', 'reenact', 'glories', 'punctu', 'accessories', 'timestream', 'gloomier', 'dragonflies', 'inquiries', 'overreacting', 'spellbook', 'analogies', 'recontextualize', 'insectoid', 'untrackable', 'fortified', 'khaliel']

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
| 31 | 2858 | 345 |
| 32 | 2891 | 348 |
| 33 | 2925 | 354 |
| 34 | 2954 | 360 |
| 35 | 2995 | 362 |
| 36 | 3015 | 362 |
| 37 | 3045 | 369 |
| 38 | 3071 | 374 |
| 39 | 3102 | 378 |
| 40 | 3136 | 387 |
| 41 | 3163 | 389 |
| 42 | 3191 | 393 |
| 43 | 3211 | 397 |
| 44 | 3238 | 401 |
| 45 | 3264 | 407 |
| 46 | 3285 | 412 |
| 47 | 3305 | 412 |
| 48 | 3335 | 419 |
| 49 | 3350 | 423 |
| 50 | 3375 | 427 |
| 51 | 3398 | 428 |
| 52 | 3419 | 430 |
| 53 | 3447 | 437 |
| 54 | 3464 | 440 |
| 55 | 3483 | 443 |
| 56 | 3505 | 447 |
| 57 | 3531 | 450 |
| 58 | 3552 | 457 |
| 59 | 3572 | 460 |
| 60 | 3591 | 462 |
| 61 | 3606 | 466 |
| 62 | 3629 | 471 |
| 63 | 3648 | 473 |
| 64 | 3667 | 480 |
| 65 | 3698 | 483 |
| 66 | 3721 | 487 |
| 67 | 3737 | 489 |
| 68 | 3751 | 493 |
| 69 | 3766 | 495 |
