"""Run the installed Gephi 0.10.1 Java modules for layout and native export."""
from pathlib import Path
import subprocess,json,hashlib,os
here=Path(__file__).resolve().parent;base=Path('/Applications/Gephi.app/Contents')
java=base/'PlugIns/jdk-11.0.17+8-jre/Contents/Home/bin/java'
jars=[p for p in (base/'Resources/gephi').rglob('*.jar') if '/locale/' not in str(p) and '/extra/' not in str(p) and 'desktop' not in p.name and (not p.name.endswith('-ui.jar') or '/platform/' in str(p)) and 'core_gephi' not in p.name]
cp=os.pathsep.join(map(str,jars));classes=Path('/tmp/hypergraph-gephi-classes');classes.mkdir(exist_ok=True)
subprocess.run([str(java),'-jar','/tmp/hypergraph-gephi-ecj.jar','-11','-proc:none','-cp',cp,'-d',str(classes),str(here/'RenderSenate.java')],check=True)
subprocess.run([str(java),'-Djava.awt.headless=true','-Dnetbeans.user=/tmp/hypergraph-gephi-user','-Dnetbeans.default_userdir_root=/tmp/hypergraph-gephi-user','-Dnetbeans.home='+str(base/'Resources/gephi/platform'),'-cp',str(classes)+os.pathsep+cp,'RenderSenate',str(here/'mixed')],check=True,timeout=120)
subprocess.run([str(java),'-Djava.awt.headless=true','-Dnetbeans.user=/tmp/hypergraph-gephi-user','-Dnetbeans.home='+str(base/'Resources/gephi/platform'),'-cp',str(classes)+os.pathsep+cp,'RenderSenate',str(here/'same_party')],check=True,timeout=120)
