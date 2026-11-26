# Finding 447: the reference was not in the box

`SVGGraphicsElement.getBBox()` on `<use>` omitted the referenced subtree:
its geometry walk saw an element with no own path and no ordinary children.
A scaled rect used at an offset answered zero rather than Chromium's
`12,24,6,8`. Phase 447 resolves local shape, group and use references and
includes their geometry with cycle protection; see
[the phase](../phases/447-the-box-of-a-use.md) for the differential and
mutation test. Referenced symbols and SVG viewports, external references,
exact rotated curve extrema, text and stroke options remain open.
