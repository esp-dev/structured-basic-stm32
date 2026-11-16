REM A recursive function and a loop over its results
FUNCTION FACT(N)
    IF N <= 1 THEN
        RETURN 1
    END IF
    RETURN N * FACT(N - 1)
END FUNCTION

FOR I = 0 TO 8
    PRINT I; "! = "; FACT(I)
NEXT I
END
