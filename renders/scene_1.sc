(oUnion
  (oNeg
    (tTrans 0 4 7 
      (mLamb 0.2 0.2 0.5 (pBox 10 8 15))))
  (tTrans -3 0 0 
    (tRot 0 1 0 0.78 
      (mLamb 0.7 0.3 0.3 (pBox 2 4 2))))
  (tTrans 3 -2 0 
    (tRot 0 0 1 0.5 
      (tRot 0 1 0 0.78 
        (mLamb 0.3 0.7 0.3 (pOctahedron 2)))))
  (tTrans 0 -2.5 5 
    (mDiel 1.5 (pSphere 1.5)))
  (tTrans 6 -2 -1.5 
    (mLamb 0.8 0.8 0.2 (pSphere 2)))
  (mEmit 1 1 1 100 (tTrans 0 15 0 
    (pSphere 4))))

