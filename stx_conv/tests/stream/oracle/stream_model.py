"""Operation-order binary32/binary64 model of stx_conv::stream.

Restated from the 2026-10-29 CONV study's NumPy streaming and bubble
references (the study itself is not retained).
`base_line` reproduces raw jets, unsigned bounds, preclipping, the fixed
five-capacity min/max network, threshold correction and synthesis order, so
the stream with bubble correction disabled must match it bit for bit.
`bubble_line` adds the bounded odd bubble with independent whole-line
indexing; the stream is compared with it to a tolerance (see
tests/stream/bubble_oracle.cpp), not bit for bit.
"""
import numpy as np

import plan_model as cm


def raw_currents(y, f):
    n = len(y)
    first = np.empty(n, dtype=f)
    second = np.empty(n, dtype=f)
    a, b, c, d = y[0], y[1], y[2], y[3]
    z, w, x, v = y[n - 1], y[n - 2], y[n - 3], y[n - 4]
    first[0] = (-f(3.0) * a + f(4.0) * b - c) / f(2.0)
    second[0] = f(2.0) * a - f(5.0) * b + f(4.0) * c - d
    first[1] = (c - a) / f(2.0)
    second[1] = a - f(2.0) * b + c
    first[n - 2] = (z - x) / f(2.0)
    second[n - 2] = x - f(2.0) * w + z
    first[n - 1] = (f(3.0) * z - f(4.0) * w + x) / f(2.0)
    second[n - 1] = f(2.0) * z - f(5.0) * w + f(4.0) * x - v
    i = np.arange(2, n - 2)
    first[i] = (y[i - 2] - f(8.0) * y[i - 1] + f(8.0) * y[i + 1] - y[i + 2]) / f(12.0)
    second[i] = (-y[i + 2] + f(16.0) * y[i + 1] - f(30.0) * y[i] + f(16.0) * y[i - 1] - y[i - 2]) / f(12.0)
    fifths = first / f(5.0)
    two_fifths = f(2.0) * first / f(5.0)
    twentieths = second / f(20.0)
    y0, y1 = y[:-1], y[1:]
    c1 = y0 + fifths[:-1]
    c2 = y0 + two_fifths[:-1] + twentieths[:-1]
    c3 = y1 - two_fifths[1:] + twentieths[1:]
    c4 = y1 - fifths[1:]
    return np.stack([c1 - y0, c2 - c1, c3 - c2, c4 - c3, y1 - c4], axis=1)


def capacity_threshold(w, mass, f):
    w=w.copy()
    for i,j in ((0,1),(3,4),(2,4),(2,3),(1,4),(0,3),(0,2),(1,3),(1,2)):
        lo=np.fmin(w[:,i],w[:,j]);hi=np.fmax(w[:,i],w[:,j])
        w[:,i]=lo;w[:,j]=hi
    result=mass/f(5)
    total=w[:,0].copy()
    result=np.fmax(result,(mass-total)/f(4))
    total=total+w[:,1];result=np.fmax(result,(mass-total)/f(3))
    total=total+w[:,2];result=np.fmax(result,(mass-total)/f(2))
    total=total+w[:,3];result=np.fmax(result,mass-total)
    return result


def admit(y, f):
    a=raw_currents(y,f);cells=len(a);zero=f(0)
    delta=y[1:]-y[:-1]
    ld=np.zeros(cells,dtype=f);rd=ld.copy();ll=ld.copy();rf=ld.copy()
    ld[1:]=delta[:-1];rd[:-1]=delta[1:];ll[1:]=a[:-1,4];rf[:-1]=a[1:,0]
    hp=np.fmin(np.fmax(ld,zero),np.fmax(np.fmax(a[:,0],zero)-np.fmax(zero-ll,zero),zero))
    hn=np.fmin(np.fmax(zero-ld,zero),np.fmax(np.fmax(zero-a[:,0],zero)-np.fmax(ll,zero),zero))
    p=np.empty_like(a);n=np.empty_like(a)
    for j in range(5):
        hp=np.fmin(hp,np.fmax(a[:,j],zero));hn=np.fmin(hn,np.fmax(zero-a[:,j],zero));p[:,j]=hp;n[:,j]=hn
    tp=np.fmin(np.fmax(rd,zero),np.fmax(np.fmax(a[:,4],zero)-np.fmax(zero-rf,zero),zero))
    tn=np.fmin(np.fmax(zero-rd,zero),np.fmax(np.fmax(zero-a[:,4],zero)-np.fmax(rf,zero),zero))
    for j in range(4,-1,-1):
        tp=np.fmin(tp,np.fmax(a[:,j],zero));tn=np.fmin(tn,np.fmax(zero-a[:,j],zero));p[:,j]=p[:,j]+tp;n[:,j]=n[:,j]+tn
    pr=np.zeros(cells,dtype=f);nr=pr.copy()
    for j in range(5):pr=pr+p[:,j];nr=nr+n[:,j]
    plus=np.fmax(delta,zero);minus=np.fmax(zero-delta,zero)
    ub=plus+np.fmin(nr,f(4)*plus);lb=minus+np.fmin(pr,f(4)*minus)
    total=np.zeros(cells,dtype=f)
    for j in range(5):
        hi=p[:,j]+ub;lo=zero-(n[:,j]+lb)
        a[:,j]=np.fmin(hi,np.fmax(lo,a[:,j]));p[:,j]=a[:,j]-lo;n[:,j]=hi-a[:,j];total=total+a[:,j]
    residual=total-delta
    down=capacity_threshold(p,np.fmax(residual,zero),f)
    up=capacity_threshold(n,np.fmax(zero-residual,zero),f)
    return (a-np.fmin(down[:,None],p))+np.fmin(up[:,None],n)


def base_line(y, m, sampling, f):
    y = np.asarray(y, dtype=f)
    n = len(y)
    if n == m:
        return y.copy()
    if n == 1:
        currents = np.zeros((0, 5), dtype=f)
    elif n < 5:
        currents = np.zeros((n - 1, 5), dtype=f)
        currents[:, 0] = y[1:] - y[:-1]
    else:
        currents = admit(y, f)
    return cm.evaluate(cm.Profile(y, currents), cm.make_plan(n, m, sampling), f)


def bubble_line(y, m, sampling, f):
    y = np.asarray(y, dtype=f)
    n = len(y)
    if n == m:
        return y.copy()
    if n == 1:
        currents = np.zeros((0, 5), dtype=f)
    elif n < 5:
        currents = np.zeros((n - 1, 5), dtype=f)
        currents[:, 0] = y[1:] - y[:-1]
    else:
        currents = admit(y, f)
    amplitude=np.zeros(max(n-1,0),dtype=f)
    if n>=5:
        raw=raw_currents(y,f)
        for cell in range(n-1):
            a=f(0)
            if cell>=2 and cell+3<n:
                v=y[cell-2:cell+4];d=v[:-1]-v[1:]
                a=f(41/30)*((d[0]-f(4)*d[1])+f(6)*d[2]-f(4)*d[3]+d[4])
            for j,w in enumerate((35,87.5,112.5,87.5,35)):a=a+f(w)*(raw[cell,j]-currents[cell,j])
            c=currents[cell]
            e=((c[0]+f(8)*c[1]+f(6)*c[2])/f(21),(c[1]+f(3)*c[2]+c[3])/f(7),(f(6)*c[2]+f(8)*c[3]+c[4])/f(21))
            lo=f(-np.inf);hi=f(np.inf)
            for v,k in zip(e,(-35,17.5,-35)):
                edge=-v*f(k)
                if v==0:lo=f(0);hi=f(0)
                elif (v>0)==(k>0):lo=np.maximum(lo,edge)
                else:hi=np.minimum(hi,edge)
            amplitude[cell]=np.minimum(hi,np.maximum(lo,a))
    result=[]
    for sample,segments in cm.make_plan(n,m,sampling):
        total=f(0)
        for idx,seg in enumerate(segments):
            value=cm.segment_value(seg,y,currents,f)
            weight=0.
            if n>=5 and seg.weights[0]!=0:
                if sample[0]=='point':
                    t=(sample[1]-seg.cell*sample[3])/sample[3];v=t*(1-t);weight=v*v*v*(2*t-1)
                else:
                    left,right,den=sample[1:];low=max(left,seg.cell*den);high=min(right,(seg.cell+1)*den)
                    t=(low-seg.cell*den)/den;u=(high-seg.cell*den)/den;v=t*(1-t);w=u*(1-u)
                    weight=(v*v*v*v-w*w*w*w)*.25*den/(right-left)
                if weight!=0:value=value+f(weight)*amplitude[seg.cell]
            total=value if idx==0 else total+value
        result.append(total)
    return np.array(result,dtype=f)


def resize(image, tw, th, sampling, f, line):
    h, w, c = image.shape
    if w == tw and h == th:
        return image.copy()

    def horizontal(img, width):
        out = np.empty((img.shape[0], width, c), dtype=f)
        for r in range(img.shape[0]):
            for ch in range(c):
                out[r, :, ch] = line(img[r, :, ch], width, sampling, f)
        return out

    def vertical(img, height):
        out = np.empty((height, img.shape[1], c), dtype=f)
        for x in range(img.shape[1]):
            for ch in range(c):
                out[:, x, ch] = line(img[:, x, ch], height, sampling, f)
        return out

    if w == tw:
        return vertical(image, th)
    if h == th:
        return horizontal(image, tw)
    if th < h:
        return horizontal(vertical(image, th), tw)
    return vertical(horizontal(image, tw), th)
