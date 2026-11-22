"""Create an H3 evidence sheet from the unmodified holdout replay (ReportLab)."""
import json
import sys
from pathlib import Path
from reportlab.lib import colors
from reportlab.pdfgen.canvas import Canvas

native, oracle, output = map(Path, sys.argv[1:])
rows = json.loads((native / 'litebird.json').read_text())
reference = json.loads((oracle / 'reference.json').read_text())
trace = next(row['trace'] for row in rows if row['id'] == 'H3')
checks = [entry for entry in trace if 'check' in entry]
c = Canvas(str(output), pagesize=(840, 725))
c.setTitle('MDN dialog: closed, open, and after Cancel')
def line(x, y, s, size=12, bold=False):
    c.setFillColor(colors.HexColor('#183845'))
    c.setFont('Helvetica-Bold' if bold else 'Helvetica', size)
    c.drawString(x, y, s)

for stage, title in [('before', 'Before opening'), ('open', 'After Update details'), ('after', 'After Cancel')]:
    line(28, 685, 'LITEBIRD   /   DIALOG DISPLAY   /   KNOWN-TASK REPLAY', 11, True)
    line(28, 635, title, 30, True)
    for x, label, directory, prefix in [(28, reference['browser'], oracle, 'ref'),
                                       (432, 'Litebird', native, 'lite')]:
        line(x, 593, label, 14, True)
        c.drawImage(str(directory / f'H3-{prefix}-{stage}.png'), x, 282, width=380, height=285)
        c.setStrokeColor(colors.HexColor('#b3c4c9'))
        c.rect(x, 282, 380, 285)
    line(28, 243, 'Frozen observations, Litebird', 14, True)
    for i, check in enumerate(checks):
        state = 'PASS' if check['got'] == check['want'] else 'FAIL'
        line(28, 217-21*i, f"{state}   {check['check']}: {check['got']}   (expected {check['want']})", 12)
    line(28, 60, 'The open-state assertion checks visibility and the open flag, not a backdrop or input isolation.', 12)
    line(28, 36, 'Pinned MDN sample a2c5719   /   800 x 600   /   This replay does not replace the first-exposure result.', 10)
    c.showPage()
c.save()
