/*
 * PROJECT:     ReactOS Local Spooler
 * LICENSE:     GNU LGPL v2.1 or any later version as published by the Free Software Foundation
 * PURPOSE:     Implementation of the Thread that actually performs the printing process
 * COPYRIGHT:   Copyright 2015 Colin Finck <colin@reactos.org>
 */

#include "precomp.h"

DWORD WINAPI
PrintingThreadProc(PLOCAL_JOB pJob)
{
    const DWORD cchMaxJobIdDigits = 5;              // Job ID is limited to 5 decimal digits, see IS_VALID_JOB_ID
    const WCHAR wszJobAppendix[] = L", Job ";
    const DWORD cchJobAppendix = _countof(wszJobAppendix) - 1;
    const WCHAR wszPortAppendix[] = L", Port";

    DWORD cchPortName;
    DWORD cchPrinterName;
    DWORD dwErrorCode;
    HANDLE hPrintProcessor = NULL;
    PLOCAL_PRINT_PROCESSOR pPrintProcessor = pJob->pPrintProcessor;
    PRINTPROCESSOROPENDATA OpenData;
    PWSTR pwszPrinterAndJob = NULL;
    PWSTR pwszPrinterPort = NULL;
    PWSTR pwszSPLFile = NULL;

    // Prepare the pPrinterName parameter.
    // This is the string for LocalOpenPrinter to open a port (e.g. "LPT1:, Port").
    cchPortName = wcslen(pJob->pPrinter->pPort->pwszName);
    pwszPrinterPort = DllAllocSplMem(cchPortName * sizeof(WCHAR) + sizeof(wszPortAppendix));
    if (!pwszPrinterPort)
    {
        dwErrorCode = ERROR_NOT_ENOUGH_MEMORY;
        ERR("DllAllocSplMem failed with error %lu!\n", GetLastError());
        goto Cleanup;
    }

    CopyMemory(pwszPrinterPort, pJob->pPrinter->pPort->pwszName, cchPortName * sizeof(WCHAR));
    CopyMemory(&pwszPrinterPort[cchPortName], wszPortAppendix, sizeof(wszPortAppendix));

    // Prepare the pPrintProcessorOpenData parameter.
    OpenData.JobId = pJob->dwJobID;
    OpenData.pDatatype = pJob->pwszDatatype;
    OpenData.pDevMode = pJob->pDevMode;
    OpenData.pDocumentName = pJob->pwszDocumentName;
    OpenData.pOutputFile = NULL;
    OpenData.pParameters = pJob->pwszPrintProcessorParameters;
    OpenData.pPrinterName = pJob->pPrinter->pwszPrinterName;

    // Open a handle to the Print Processor.
    hPrintProcessor = pPrintProcessor->pfnOpenPrintProcessor(pwszPrinterPort, &OpenData);
    if (!hPrintProcessor)
    {
        dwErrorCode = GetLastError();
        ERR("OpenPrintProcessor failed with error %lu!\n", dwErrorCode);
        goto Cleanup;
    }

    // Let other functions use the Print Processor as well while it's opened.
    pJob->hPrintProcessor = hPrintProcessor;

    // Prepare the pDocumentName parameter.
    cchPrinterName = wcslen(OpenData.pPrinterName);
    pwszPrinterAndJob = DllAllocSplMem((cchPrinterName + cchJobAppendix + cchMaxJobIdDigits + 1) * sizeof(WCHAR));
    if (!pwszPrinterAndJob)
    {
        dwErrorCode = ERROR_NOT_ENOUGH_MEMORY;
        ERR("DllAllocSplMem failed with error %lu!\n", GetLastError());
        goto Cleanup;
    }

    CopyMemory(pwszPrinterAndJob, OpenData.pPrinterName, cchPrinterName * sizeof(WCHAR));
    CopyMemory(&pwszPrinterAndJob[cchPrinterName], wszJobAppendix, cchJobAppendix * sizeof(WCHAR));
    _ultow(OpenData.JobId, &pwszPrinterAndJob[cchPrinterName + cchJobAppendix], 10);

    // Print the document.
    // Note that pJob is freed after this function, so we may not access it anymore.
    if (!pPrintProcessor->pfnPrintDocumentOnPrintProcessor(hPrintProcessor, pwszPrinterAndJob))
    {
        dwErrorCode = GetLastError();
        ERR("PrintDocumentOnPrintProcessor failed with error %lu!\n", dwErrorCode);
        goto Cleanup;
    }

    // Close the Print Processor.
    pPrintProcessor->pfnClosePrintProcessor(hPrintProcessor);
    hPrintProcessor = NULL;

    // Delete the spool file.
    pwszSPLFile = DllAllocSplMem(GetJobFilePath(L"SPL", 0, NULL));
    if (!pwszSPLFile)
    {
        dwErrorCode = ERROR_NOT_ENOUGH_MEMORY;
        ERR("DllAllocSplMem failed with error %lu!\n", GetLastError());
        goto Cleanup;
    }

    GetJobFilePath(L"SPL", OpenData.JobId, pwszSPLFile);
    DeleteFileW(pwszSPLFile);

    // We were successful!
    dwErrorCode = ERROR_SUCCESS;

Cleanup:
    if (hPrintProcessor)
        pPrintProcessor->pfnClosePrintProcessor(hPrintProcessor);

    if (pwszPrinterPort)
        DllFreeSplMem(pwszPrinterPort);

    if (pwszPrinterAndJob)
        DllFreeSplMem(pwszPrinterAndJob);

    if (pwszSPLFile)
        DllFreeSplMem(pwszSPLFile);

    return dwErrorCode;
}