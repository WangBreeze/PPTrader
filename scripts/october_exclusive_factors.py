"""Compare pre-entry factors for a supplied October mechanical trade ledger."""
import argparse
import base64
import csv
from decimal import Decimal
import gzip
import hashlib
import json
import operator
from pathlib import Path
import re
from tempfile import TemporaryDirectory

import numpy as np
import pandas as pd

DATA = Path(__file__).resolve().parents[2] / 'RD-agent--PPTrader'
KEY = ['买入日期', '股票代码', '买入时间']
OPS = {'<':operator.lt,'>':operator.gt,'<=':operator.le,'>=':operator.ge,'==':operator.eq}
PHASES = ['发现','隔开','验证']


def part(path):
    js=path.read_text()
    args=js.split('.accept(',1)[1].rstrip().removesuffix(');')
    tag,rows,packed=json.loads('['+args+']')
    values=np.frombuffer(gzip.decompress(base64.b64decode(packed)),dtype='<f8').reshape(len(rows),432)
    return rows,values


def matrices(trades,temp,views):
    index=(views/'changes/index.js').read_text()
    meta=json.loads(index.split('const stats = ',1)[1].split(';\n',1)[0])
    mapping=np.frombuffer(gzip.decompress(base64.b64decode(re.search(r"const mapData = '([^']+)'",index)[1])),dtype='<u4').reshape(-1,3)
    needed=int(mapping[:,0].max())+1
    opens=np.memmap(temp/'open',mode='w+',dtype=float,shape=(needed,432))
    keys=[]
    for i in range((needed+511)//512):
        rows,vals=part(views/f'changes/o{i}.js')
        count=min(len(rows),needed-len(keys))
        opens[len(keys):len(keys)+count]=vals[:count]
        keys.extend((r[0],r[1]) for r in rows[:count])
    mats=[np.memmap(temp/k,mode='w+',dtype=float,shape=(len(trades),432),order='F') for k in ('entry','delta')]
    for m in mats:m[:]=np.nan
    lookup={k:i for i,k in enumerate(trades[KEY].itertuples(index=False,name=None))}
    assert len(lookup)==len(trades) and len(meta)==432
    matched=set()
    for i in range((len(mapping)+511)//512):
        rows,vals=part(views/f'entry/block-{i}.js')
        src=[];dst=[];prev=[]
        for j,r in enumerate(rows):
            date=f'{r[1][:4]}-{r[1][4:6]}-{r[1][6:]}'
            key=(date,r[2],r[3]); ix=lookup.get(key)
            if ix is None:continue
            assert ix not in matched and (not np.isfinite(vals[j]).any() or (r[4] and r[4]<r[3]))
            pos=int(mapping[i*512+j,0])
            assert int(r[0])==i*512+j+1 and keys[pos][0]<date and keys[pos][1]==r[2]
            matched.add(ix);src.append(j);dst.append(ix);prev.append(pos)
        if src:
            mats[0][dst,:]=vals[src];mats[1][dst,:]=vals[src]-opens[prev]
    return mats,meta,len(matched)


def assess(values,kind,factor,title,y,days,sections,rows,dist,mode=None):
    finite=np.isfinite(values)
    train=sections[0]&finite
    if not train.any():return
    d={'因子':factor,'名称':title,'输入':kind}
    for phase,m in zip(PHASES,sections):
        for label,group in [('达标',y),('未达标',~y)]:
            a=values[m&finite&group]
            d[phase+label+'有效']=len(a)
            d[phase+label+'中位数']=float(np.median(a)) if len(a) else None
    dist.append(d)
    if mode:
        rules=[('==',1.)]
    else:
        q=np.quantile(values[train],[.01,.05,.10,.90,.95,.99])
        rules=[('<=' if i<3 else '>=',float(v)) for i,v in enumerate(q)]
        neg=values[train&~y]
        if len(neg):rules.extend([('<',float(neg.min())),('>',float(neg.max()))])
    for op,cut in dict.fromkeys(rules):
        hits=finite&OPS[op](values,cut)
        counts=np.bincount(days[hits],minlength=17)
        wins=np.bincount(days[hits&y],minlength=17)
        record={'因子':factor,'名称':title,'输入':kind,'方向':op,'阈值':cut}
        for phase,sl in zip(PHASES,(slice(0,9),slice(9,10),slice(10,17))):
            w=int(wins[sl].sum());n=int(counts[sl].sum())
            record.update({phase+'达标':w,phase+'反例':n-w,phase+'达标天数':int((wins[sl]>0).sum()),phase+'命中达标率':w/n if n else 0})
        rows.append(record)


def analyze(path,out,extra=None):
    out.mkdir(parents=True,exist_ok=True)
    trades=pd.read_csv(path,dtype={'股票代码':str,'买入价格':str,'盈利金额':str})
    y=np.array([Decimal(p)>Decimal('.02')*Decimal(b)*int(n) for p,b,n in zip(trades['盈利金额'],trades['买入价格'],trades['数量'])])
    assert np.array_equal(y,trades['净收益超2%'].eq('是').to_numpy())
    dates,days=np.unique(trades['买入日期'],return_inverse=True)
    assert len(dates)==17 and dates[10]=='2026-10-22'
    sections=(days<9,days==9,days>=10)
    inputs=DATA/'views/october-factors'
    records=[];dist=[]
    with TemporaryDirectory() as t:
        mats,meta,matched=matrices(trades,Path(t),inputs)
        if extra:
            with np.load(extra,allow_pickle=False) as suppl:
                ids=suppl['ids']; dest=trades.set_index('编号').index.get_indexer(ids)
                assert min(dest)>=0 and len(set(dest))==len(dest)
                for mat,key in zip(mats,['entry','delta']):
                    assert not np.isfinite(mat[dest]).any() and suppl[key].shape==(len(dest),432)
                    mat[dest]=suppl[key]
        coverage=[int(np.isfinite(np.asarray(m)).any(axis=1).sum()) for m in mats]
        for typ,mat in zip(['入场前值','相对前日开盘差值'],mats):
            for j,f in enumerate(meta):
                assess(mat[:,j],typ,f['id'],f['title'],y,days,sections,records,dist)
    auxcols=['3日等权量变（%）','3日等权成交额变化（%）','入场前高低点顺序']
    aux=pd.read_csv(DATA/'runs/R0084/10月逐笔高低点量额.csv',dtype={'股票代码':str},usecols=KEY+auxcols)
    joined=trades[KEY].merge(aux,on=KEY,how='left',sort=False,validate='one_to_one')
    for c in auxcols[:2]:assess(joined[c].to_numpy(), '入场前值',c,c,y,days,sections,records,dist)
    for name in ('先低后高','先高后低','一价'):
        s=joined[auxcols[2]];x=np.where(s.isin(['先低后高','先高后低','一价']),s.eq(name).astype(float),np.nan)
        assess(x,'形态',name,name,y,days,sections,records,dist,True)
    allrules=pd.DataFrame(records)
    allowed=allrules['发现达标'].ge(10)&allrules['发现达标天数'].ge(3)
    zero=allrules['发现反例'].eq(0)&allrules['发现达标'].gt(0)
    allrules['发现期锁定独有候选']=allowed&zero
    confirmed=allowed&zero&allrules['隔开反例'].eq(0)&allrules['验证反例'].eq(0)&allrules['验证达标'].ge(5)&allrules['验证达标天数'].ge(2)
    allrules['十月样本内独有']=confirmed
    for phase,mask in zip(PHASES,sections):
        allrules[phase+'达标召回率']=allrules[phase+'达标']/y[mask].sum()
    ranking=allrules.loc[allowed & allrules['发现反例'].gt(0)].sort_values(['发现命中达标率','发现达标'],ascending=False,kind='stable').drop_duplicates('因子')
    for frame,file in [(allrules,'全部规则'),(pd.DataFrame(dist),'因子覆盖及两组中位数'),(allrules[confirmed],'样本内独有'),(allrules[allowed&zero],'发现期独有候选'),(ranking,'发现期排序及验证表现')]:
        frame.to_csv(out/(file+'.csv'),index=False,encoding='utf-8-sig',float_format='%.17g')
    summary={'交易':len(trades),'达标':int(y.sum()),'未达标':int((~y).sum()),'已匹配原因子截点':matched,'入场因子至少一项有效':coverage[0],'差值至少一项有效':coverage[1],'原始因子数':len(meta),'测试规则数':len(records),'发现零反例但支持不足规则数':int((zero&~allowed).sum()),'发现独有候选':int((allowed&zero).sum()),'验证后独有':int(confirmed.sum()),'各期':{k:{'日期':list(dates[np.unique(days[m])]),'交易':int(m.sum()),'达标':int(y[m].sum())} for k,m in zip(PHASES,sections)},'成交清单SHA256':hashlib.sha256(path.read_bytes()).hexdigest()}
    (out/'执行摘要.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(summary,ensure_ascii=False))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trades',type=Path);parser.add_argument('out',type=Path)
    parser.add_argument('--extra-factors',type=Path,help='由等时钟因子管线复算的缺失截点，NPZ ids/entry/delta')
    args=parser.parse_args()
    analyze(args.trades,args.out,args.extra_factors)
