"""Place native Gephi SVG artwork in the paper without abbreviating names."""
from pathlib import Path
import json,subprocess,xml.etree.ElementTree as ET
from matplotlib.textpath import TextPath
from matplotlib.font_manager import FontProperties
P=Path(__file__).resolve().parent;R=P.parents[3]
SVG='http://www.w3.org/2000/svg';ET.register_namespace('',SVG)
font=FontProperties(family='DejaVu Sans');report={}
for stage in ['mixed','same_party']:
 source=P/stage/'community_before.svg';tree=ET.parse(source);root=tree.getroot();x,y,w,h=map(float,root.attrib['viewBox'].split());left,right=x,x+w
 for t in root.iter('{'+SVG+'}text'):
  text=''.join(t.itertext()).strip();size=11.0
  t.set('font-size',str(size));t.set('font-family','DejaVu Sans');t.set('paint-order','stroke fill');t.set('stroke','white');t.set('stroke-width','0.8');t.set('stroke-linejoin','round')
  width=TextPath((0,0),text,size=size,prop=font).get_extents().width
  cx=float(t.attrib['x']);left=min(left,cx-width/2-2);right=max(right,cx+width/2+2)
 # Only extend clipping bounds; graph coordinates and native labels stay fixed.
 box=(left,y-1,right-left,h+2)
 root.set('viewBox',' '.join(map(str,box)));root.set('width',str(box[2]));root.set('height',str(box[3]))
 out=R/'paper/figures'/f'case_senate_{stage}_fullnames.svg';tree.write(out,encoding='utf-8',xml_declaration=True)
 subprocess.run(['/Users/xiaoyu/anaconda3/bin/rsvg-convert','-f','pdf','-o',str(out.with_suffix('.pdf')),str(out)],check=True)
 scale=min((.49*241.14749)/box[2],(1.17*72.27)/box[3]);effective=11*scale
 assert effective>=6.5,(stage,effective,box)
 report[stage]=dict(gephi_source=str(source.relative_to(R)),viewbox=box,minimum_paper_font_pt=effective,labels='full original surnames',topology_and_coordinates='unchanged native Gephi SVG')
(R/'output/figure_qa/case_study/senate/fullnames_layout.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

# Match native Gephi role encoding: color indicates party, size indicates query.
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':6.5,'pdf.fonttype':42})
fig=plt.figure(figsize=(.78*241.14749/72.27,.17))
fig.legend(handles=[Line2D([],[],ls='',marker='o',mfc=c,mec=c,ms=ms,label=label) for c,ms,label in [('#0072B2',4,'Democrat'),('#D55E00',4,'Republican'),('black',6,'Query')]],loc='center',ncol=3,frameon=False,fontsize=6.5,handletextpad=.3,columnspacing=.7)
fig.savefig(R/'paper/figures/case_senate_legend.pdf');plt.close(fig)
