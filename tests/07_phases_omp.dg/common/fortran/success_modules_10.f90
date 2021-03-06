! <testinfo>
! test_generator=config/mercurium-omp
! test_nolink=yes
! </testinfo>
MODULE KINDS
IMPLICIT NONE
INTEGER, PARAMETER :: DP = 8
END MODULE KINDS

MODULE NONCOLLIN_MODULE
IMPLICIT NONE
INTEGER :: NSPIN_MAG
END MODULE NONCOLLIN_MODULE


MODULE PAW_ONECENTER
    USE KINDS,          ONLY : DP
    IMPLICIT NONE
 CONTAINS

SUBROUTINE PAW_XC_POTENTIAL()
    USE NONCOLLIN_MODULE,       ONLY : NSPIN_MAG
    REAL(8), ALLOCATABLE :: G_RAD(:)       ! RADIAL POTENTIAL
    INTEGER               :: IX

!$OMP DO
    DO IX =1, 10
        NSPIN_MAG = NSPIN_MAG + 1
        CALL COMPUTE_G(G_RAD)
    ENDDO
    RETURN
END SUBROUTINE PAW_XC_POTENTIAL
!
 SUBROUTINE COMPUTE_G(G_RAD)
     USE NONCOLLIN_MODULE, ONLY : NSPIN_MAG
         IMPLICIT NONE
    REAL(8), INTENT(IN)    :: G_RAD(NSPIN_MAG) ! RADIAL POT 
    RETURN
 END SUBROUTINE COMPUTE_G

END MODULE PAW_ONECENTER
