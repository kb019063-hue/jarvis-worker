# 478. parseInt repeatedly rounded power-of-two digits

Accumulating each power-of-two radix digit as a double lost precision on long
integers. Phase 478 retains the significant bits and rounds once, including
ties and overflow. Python/Node comparison improved **1,320/1,506 → 1,506/1,506**;
test262 remained 54/56. See [Phase 478](../phases/478-parse-int-power-radices.md).
Non-power-of-two radices were not audited.
