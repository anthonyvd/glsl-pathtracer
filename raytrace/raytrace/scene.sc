(oUnion
  (tRot 0 1 0 (.time)
    (oUnion
      (tTrans 5 0 0 (mEmit 1 0 0 10 (pSphere 0.5)))
      (tTrans -5 0 0 (mEmit 0 1 0 10 (pSphere 0.5)))))
  (oSmoothUnion 0.2
    (mLamb 0.3 0.3 0.3 (pSphere 1))
    (tTrans 0 0.7 0 (mLamb 0.3 0.3 0.3 (pSphere 1))))
  (mLamb 0.7 0.7 0.7 (pPlane 0 1 0 2)))
