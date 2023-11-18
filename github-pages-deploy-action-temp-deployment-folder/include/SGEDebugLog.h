/** @file SGEDebugLog.h
 *
 *  Provides debug output to a log, for target device SW development.
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef DEBUGLOG_H
#define DEBUGLOG_H

#include <e32std.h>
#include <f32file.h>

/**
 * @name   Class RSGEDebugLog
 *
 * @class  RSGEDebugLog
 *
 * @brief  Provides output to a debug log on device (E:\debug.log).  This
 *         slows down performance markedly.
 *
 * @remark Note: Enable logging by defining ENABLE_LOG.
 */
class RSGEDebugLog
{
public:
    enum TLogLevel { ENone, ECritical, EMajor, EAll };

    /**
     * @name  RSGEDebugLog
     *
     * @fn    RSGEDebugLog()
     *
     * @brief Default C++ Constructor.
     *
     */
    IMPORT_C RSGEDebugLog();

    /**
     * @name ~RSGEDebugLog
     *
     * @fn   ~RSGEDebugLog()
     *
     * @brief Destructor.
     *
     */
    IMPORT_C virtual ~RSGEDebugLog();

    /**
     * @name   Open
     *
     * @fn     TInt Open()
     *
     * @brief  Creates the log file (note this will overwrite any
     *         previous instance of the log file).
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt Open();

    /**
     * @name  Close
     *
     * @fn    void Close();
     *
     * @brief Closes the debug log file.
     */
    IMPORT_C void Close();

    /**
     * @name   WriteLine
     *
     * @fn     TInt WriteLine(const char* aText, TLogLevel aLevel = EAll)
     *
     * @brief  Writes a line to the debug log file.
     *
     * @param  aText ANSI text to write (can be string literal)
     *
     * @param  Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt WriteLine(const char* aText, TLogLevel aLevel = EAll);

    /**
     * @name   WriteLine
     *
     * @fn     TInt WriteLine(const char* aText, TInt aInt, TLogLevel aLevel = EAll)
     *
     * @brief  Writes a line to the debug log file.
     *
     * @param  aText ANSI text to write (can be string literal)
     *
     * @param  Integer to be written as string at the end of the line
     *
     * @param  Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt WriteLine(const char* aText, TInt aInt, TLogLevel aLevel = EAll);

    /**
     * @name   WriteLine
     *
     * @fn     TInt WriteLine(const char* aText, TInt aInt, TLogLevel aLevel = EAll)
     *
     * @brief  Writes a line to the debug log file.
     *
     * @param  aText ANSI text to write (can be string literal)
     *
     * @param  Integer to be written as hex string at the end of the line
     *
     * @param  Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt WriteLine(const char* aText, TUint32 aHex, TLogLevel aLevel = EAll);

    /**
     * @name   Write
     *
     * @fn     TInt Write(const char* aText, TLogLevel aLevel = EAll)
     *
     * @brief  Writes text to the debug log file.
     *
     * @param  aText  ANSI text to write (can be string literal)
     *
     * @param  aLevel Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt Write(const char* aText, TLogLevel aLevel = EAll);

    /**
     * @name   Write
     *
     * @fn     Write(const TInt aInt, TLogLevel aLevel = EAll)
     *
     * @brief  Writes integer as a string to the debug log file.
     *
     * @param  aInt   Integer to be written as a string
     *
     * @param  aLevel Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt Write(const TInt aInt, TLogLevel aLevel = EAll);

    /**
     * @name   Write
     *
     * @fn     TInt Write(const TUint32 aHex, TLogLevel aLevel = EAll)
     *
     * @brief  Writes integer as a hex string to the debug log file.
     *
     * @param  aHex   Integer to be written as a hex string
     *
     * @param  aLevel Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt Write(const TUint32 aHex, TLogLevel aLevel = EAll);

    /**
     * @name   Write
     *
     * @fn     TInt Write(const TUint8 aHex, TLogLevel aLevel = EAll)
     *
     * @brief  Writes integer as a hex string to the debug log file.
     *
     * @param  aHex   Integer to be written as a hex string
     *
     * @param  aLevel Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt Write(const TUint8 aHex, TLogLevel aLevel = EAll);

    /**
     * @name   Write
     *
     * @fn     TInt Write(const char aChar, TLogLevel aLevel = EAll)
     *
     * @brief  Writes character to the debug log file.
     *
     * @param  aChar  ANSI character to be written
     *
     * @param  aLevel Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt Write(const char aChar, TLogLevel aLevel = EAll);

    /**
     * @name   Write
     *
     * @fn     TInt Write(const char aChar, TLogLevel aLevel = EAll)
     *
     * @brief  Writes text to the debug log file.
     *
     * @param  aChar  Descriptor containing text to write to the log
     *
     * @param  aLevel Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt Write(const TDesC& aDesC, TLogLevel aLevel = EAll);

    /**
     * @name   NewLine
     *
     * @fn     TInt NewLine(TLogLevel aLevel = EAll)
     *
     * @brief  Writes a newline character to the current position in the
     *         debug log file.
     *
     * @param  aLevel Priority of logging - UI allows this to be set
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt NewLine(TLogLevel aLevel = EAll);

    /**
     * @name  SetLoggingLevel
     *
     * @fn    void SetLoggingLevel(TLogLevel aLevel)
     *
     * @brief Determines the minimum priority level required for the
     *        entry to be written to the log.
     *
     *        It is useful if this can be set by a UI component in the
     *        game to avoid recompilation of code.
     *
     * @param aLevel Priority of logging - UI allows this to be set
     */
    IMPORT_C void SetLoggingLevel(TLogLevel aLevel);

protected:
    RFile     iDebugLogFile;
    RFs       iDebugLogFs;
    TLogLevel iLoggingLevel;
};

#endif // DEBUGLOG_H
