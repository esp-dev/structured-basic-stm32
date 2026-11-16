REM FOR, IF and formatted output
PRINT " N  |  N x N"
PRINT "----+-------"
FOR N = 1 TO 10
    PRINT N; "   |  "; N * N
NEXT N
IF N >= 10 THEN
    PRINT "Table complete."
END IF
END
