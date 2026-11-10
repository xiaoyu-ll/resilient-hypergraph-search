"""Show every original interaction of the discovered community, then fail a seed."""
from pathlib import Path
import json,sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon
from matplotlib.lines import Line2D
ROOT=Path(__file__).resolve().parents[2];P=Path(__file__).resolve().parent
sys.path.insert(0,str(Path.home()/'.codex/skills/scipilot-figure-skill/scripts'))
from setup_style import setup_style
from visual_qa import audit_layout
setup_style(journal='ieee',lang='en',use_sciplots=False,constrained_layout=False)
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':6.5,'pdf.fonttype':42,'svg.fonttype':'none'})
pos={6765:(.5,.80),6467:(.12,.80),197410:(.27,.98),6466:(.10,.45),88621:(.28,.18),139584:(.82,.22),134496:(.94,.50),30517:(.85,.80),63021:(.68,.98),107893:(.63,.53),6527:(.45,.045)}
labels={6765:'Mou',6467:'Has',197410:'Tah',6466:'Nas',88621:'Tal',139584:'Rah',134496:'Kha',30517:'Boc',63021:'Maa',107893:'Haf',6527:'Bou'}
data=json.loads((P/'community_structure.json').read_text());edges=data['edges'];assert len(edges)==14
qa=ROOT/'output/figure_qa/case_study/community';qa.mkdir(parents=True,exist_ok=True)
for failed,name in [(None,'before'),(6765,'after')]:
 fig=plt.figure(figsize=(.49*241.14749/72.27,1.1),layout='none');ax=fig.add_axes([0,0,1,1]);ax.set(xlim=(-.02,1.06),ylim=(-.07,1.12));ax.axis('off')
 triples=[e for e in edges if len(e)>2];colors=['#0072B2','#009E73','#D55E00']
 for e in edges:
  if failed in e:continue
  xy=[pos[v] for v in e]
  if len(e)==2:ax.plot(*zip(*xy),lw=.75,color='#777777',zorder=1)
  else:ax.add_patch(Polygon(xy,closed=True,facecolor=colors[triples.index(e)]+'25',edgecolor=colors[triples.index(e)],lw=1,zorder=0))
 for v,(x,y) in pos.items():
  dead=v==failed;seed=v in [6765,139584]
  ax.plot(x,y,marker='x' if dead else ('s' if seed else 'o'),markersize=13 if not dead else 9,markeredgewidth=.7,color='#999999' if dead else '#222222',markerfacecolor=('#222222' if seed else 'white'),zorder=4)
  ax.text(x,y+.13 if dead else y,labels[v],ha='center',va='center',fontsize=6.5,color='white' if seed and not dead else '#222222',zorder=5)
 issues=audit_layout(fig);assert not issues,issues
 fig.savefig(qa/(name+'.png'),dpi=400);fig.savefig(ROOT/'paper/figures'/f'case_dblp_{name}.pdf');fig.savefig(ROOT/'paper/figures'/f'case_dblp_{name}.svg');plt.close(fig)
fig=plt.figure(figsize=(.78*241.14749/72.27,.17),layout='none')
fig.legend(handles=[Line2D([],[],marker=m,ls='',color='#222222',mfc=fc,ms=5,label=l) for m,fc,l in [('s','#222222','Query'),('o','white','Member'),('x','white','Failed')]],loc='center',ncol=3,frameon=False,fontsize=7)
fig.savefig(ROOT/'paper/figures/case_dblp_legend.pdf');plt.close(fig)
