# Original AdLib cue captures

These DOSBox Raw OPL 2.0 (`.dro`) files are lossless YM3812 register traces
captured from the supplied `NM.EXE` while it consumed the supplied `NM.RES`
sound bank. They are preservation assets, not independently composed
replacements. The native executable embeds the traces and renders them with a
YM3812 core at run time.

| File | Trigger isolated in the DOS reference | Header duration | SHA-256 |
| --- | --- | ---: | --- |
| `board-start-adlib.dro` | Enter on Prime Numbers in the game selector | 219 ms | `5023EF2AFB32C7176DC059D8E5BB913A24B66067CB29A3A395620B5E3A6E42C1` |
| `correct-munch-adlib.dro` | Correct level-1 Prime munch (resource sequence 26) | 260 ms | `4B467C3A0F89026EB9B0E1833F68552470FFB7DCE9A271CCD7E6341D47C1A995` |
| `level-advance-adlib.dro` | Final correct munch, level-advance fanfare, and level-2 board start | 1,112 ms | `9B96D911FDFCD2B4A07AB7EEE4CE7E5CB4D0EB058149DA64B34AE298182D4270` |
| `wrong-munch-adlib.dro` | Wrong level-1 Prime munch and feedback cue | 274 ms | `D21CACCD5BFE61D2081A86D6C4AC8A2BBDF8EA136862AB2AB695545F3307DD7C` |
| `troggle-collision-adlib.dro` | Reggie collision/death and feedback cue | 1,647 ms | `B62E69AE36725E90114A4F6FA12B93CDE3BA24B2DF2B344F36EA0F650F495C3A` |

The capture oracle used DOSBox-X's raw FM recorder. Initial YM3812 state writes
are intentionally retained so each clip is deterministic and self-contained.
