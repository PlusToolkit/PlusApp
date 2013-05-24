/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#pragma once

#include <windows.h>

typedef struct {
 LARGE_INTEGER start;
 LARGE_INTEGER stop;
} stopWatch;


class Timer
{
private:
     stopWatch timer;
     LARGE_INTEGER frequency;
     double LIToSecs( LARGE_INTEGER & L) ;
 public:
     Timer() ;
     void startTimer( ) ;
     void stopTimer( ) ;
     double getElapsedTime() ;
};