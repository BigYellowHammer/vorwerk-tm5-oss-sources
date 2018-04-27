! { dg-do run }
IF (T1(1.0,1.0) .NE. (1.0,1.0) ) CALL ABORT()
IF (T1(1.0) .NE. (1.0,0.0)) CALL ABORT()
IF (M1(1,2,3) .NE. 3) CALL ABORT()
IF (M1(1,2,A4=4) .NE. 4) CALL ABORT()
CONTAINS

COMPLEX FUNCTION T1(X,Y)
  REAL :: X
  REAL, OPTIONAL :: Y
  T1=CMPLX(X,Y)
END FUNCTION T1

INTEGER FUNCTION M1(A1,A2,A3,A4)
  INTEGER :: A1,A2
  INTEGER, OPTIONAL :: A3,A4
  M1=MAX(A1,A2,A3,A4)
END FUNCTION M1

END