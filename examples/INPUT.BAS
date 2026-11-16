REM Type text and a number at the prompts
INPUT "Your name: "; WHO$
INPUT "An integer from 1 to 100: "; N
IF N < 1 OR N > 100 THEN
    PRINT "Please run again and enter a number from 1 to 100."
ELSE
    PRINT "Hello, "; WHO$; "!"
    PRINT N; " squared is "; N * N
END IF
END
