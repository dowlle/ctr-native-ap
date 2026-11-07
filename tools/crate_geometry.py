"""A3-33 geometry rule, report-only. Requires numpy and editor levtool."""
import numpy as np

TRIS=[(0,4,5),(4,6,5),(6,4,1),(5,6,2),(8,6,7),(7,3,8),(1,7,6),(2,6,8)]

def triangles(lev, road=True):
    vs=np.array([v['position'] for v in lev.vertices],dtype=float)
    ts=[]; ids=[]
    for q in lev.quadblocks:
        f=q['flags']
        if road and (not f&0x1000 or f&(0x10|0x40|0x200)): continue
        ix=q['vertex_indices']
        for tri in TRIS[:4 if ix[2]==ix[3] else 8]:
            p=vs[[ix[j] for j in tri]]
            n=np.cross(p[1]-p[0],p[2]-p[0]); norm=np.linalg.norm(n)
            # Ground flag is authoritative; reject vertical/near-vertical faces.
            if norm<1 or (road and abs(n[1])/norm < .5): continue
            ts.append(p); ids.append(q['index'])
    return np.array(ts),np.array(ids)

def measure(p, ts, ids):
    if not len(ts): return dict(distance=1e9,horizontal=1e9,dy=1e9,quad=None,nearest=None)
    p=np.array(p,dtype=float); a,b,c=ts[:,0],ts[:,1],ts[:,2]
    ab=b-a; ac=c-a; ap=p-a
    d00=np.sum(ab*ab,axis=1); d01=np.sum(ab*ac,axis=1); d11=np.sum(ac*ac,axis=1)
    d20=np.sum(ap*ab,axis=1); d21=np.sum(ap*ac,axis=1)
    den=d00*d11-d01*d01
    v=(d11*d20-d01*d21)/den; w=(d00*d21-d01*d20)/den
    proj=a+v[:,None]*ab+w[:,None]*ac
    points=[proj]; ds=[np.where((v>=0)&(w>=0)&(v+w<=1),np.sum((proj-p)**2,axis=1),np.inf)]
    for x,y in ((a,b),(b,c),(c,a)):
        edge=y-x; length=np.sum(edge*edge,axis=1)
        t=np.clip(np.sum((p-x)*edge,axis=1)/np.maximum(length,1e-20),0,1)
        pt=x+t[:,None]*edge; points.append(pt); ds.append(np.sum((pt-p)**2,axis=1))
    ds=np.array(ds); ii,jj=np.unravel_index(np.argmin(ds),ds.shape)
    pt=points[ii][jj]; delta=p-pt
    # Exact XZ distance to each triangle clipped to the height slab.
    # Candidates: vertical projection into face, clipped original edges,
    # and the two new edges where the height slab cuts the face.
    lo,hi=p[1]-384,p[1]+128
    def segment(x,y):
        e=y-x; lens=np.sum(e[:,[0,2]]**2,axis=1)
        t=np.clip(np.sum((p-x)[:,[0,2]]*e[:,[0,2]],axis=1)/np.maximum(lens,1e-20),0,1)
        return x+t[:,None]*e
    ab2=ab[:,[0,2]]; ac2=ac[:,[0,2]]; ap2=ap[:,[0,2]]
    det=ab2[:,0]*ac2[:,1]-ab2[:,1]*ac2[:,0]
    det=np.where(abs(det)<1e-12,np.nan,det)
    vv=(ap2[:,0]*ac2[:,1]-ap2[:,1]*ac2[:,0])/det
    ww=(ab2[:,0]*ap2[:,1]-ab2[:,1]*ap2[:,0])/det
    face=a+vv[:,None]*ab+ww[:,None]*ac
    candidates=[face]; masks=[(vv>=0)&(ww>=0)&(vv+ww<=1)&(face[:,1]>=lo)&(face[:,1]<=hi)]
    cuts={lo:[],hi:[]}
    for x,y in ((a,b),(b,c),(c,a)):
        e=y-x; ey=e[:,1]; safe=np.where(abs(ey)<1e-12,1,ey)
        t0=(lo-x[:,1])/safe; t1=(hi-x[:,1])/safe
        low=np.maximum(0,np.minimum(t0,t1)); high=np.minimum(1,np.maximum(t0,t1))
        flat=abs(ey)<1e-12
        low=np.where(flat,0,low);high=np.where(flat,1,high)
        valid=(low<=high)&(~flat|((x[:,1]>=lo)&(x[:,1]<=hi)))
        candidates.append(segment(x+low[:,None]*e,x+high[:,None]*e));masks.append(valid)
        for height in cuts:
            t=(height-x[:,1])/safe
            cuts[height].append((x+t[:,None]*e,(~flat)&(t>=0)&(t<=1)))
    for cut in cuts.values():
        for i,j in ((0,1),(0,2),(1,2)):
            candidates.append(segment(cut[i][0],cut[j][0]));masks.append(cut[i][1]&cut[j][1])
    candidates=np.array(candidates); dy=p[1]-candidates[:,:,1]
    horizontal=np.linalg.norm((candidates-p)[:,:,[0,2]],axis=2)
    horizontal=np.where(np.array(masks),horizontal,np.inf)
    si,sj=np.unravel_index(np.argmin(horizontal),horizontal.shape)
    return dict(distance=float(np.sqrt(ds[ii,jj])), horizontal=float(horizontal[si,sj]) if np.isfinite(horizontal[si,sj]) else 1e9,
                dy=float(dy[si,sj]),quad=int(ids[sj]),nearest=candidates[si,sj].tolist(),nearest_3d=pt.tolist(),quad_3d=int(ids[jj]))
