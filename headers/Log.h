/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#pragma once
#ifndef LOG_H
#define LOG_H

// ~ O Código ta no log C
// Escrita baixo nivel, sem stdio bufferizado:
// Windows -> WriteFile, resto -> write(fileno).
void Log_Info(const char* msg);
void Log_Error(const char* msg);
void Log_Fatal(const char* msg); // stderr + exit(1), nao retorna
void Log_ErrorF(const char* fmt, ...);
void Log_FatalF(const char* fmt, ...); // nao retorna

#endif
