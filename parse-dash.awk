#!/bin/awk -f
BEGIN {

  Time = 0;
  bpsAverage = 0;
  bpsLastChunk = 0;
  nextBitrate = 0;
  chunkCount = 0;
  totalChunks = 0;
  downloadDuration = 0;

  bufferSize = 0;
  hasThroughput = 0;
  estimatedBW = 0;
  videoLevel = 0;

  state = 0;

  print "Time", "bpsAverage", "bpsLastChunk", "nextBitrate", "chunkCount", "totalChunks", "downloadDuration" > "statistics.dat";
  print "Time", "bufferSize", "hasThroughput", "estimatedBW", "videoLevel" > "buffer.dat";
}
{
  if ($1 == "=======START===========") {
      state = 1;
  }
  else if ($1 == "=======BUFFER==========") {
      state = 2;
  }

  if ($1 == "Time:" && state == 1) {
    Time = $2/1000;
    bpsAverage = $4;
    bpsLastChunk = $6;
    nextBitrate = $8;
    chunkCount = $10;
    totalChunks = $12;
    downloadDuration = $14;
    
    printf "%.2f %.2f %d %d %d %d %.2f\n", Time, bpsAverage, bpsLastChunk, nextBitrate, chunkCount, totoalChunks, downloadDuration > "statistics.dat";
  }
  else if ($1 == "Time:" && state == 2) {
    Time = $2/1000;
    bufferSize = $4;
    hasThroughput = $6;
    estimatedBW = $8;
    videoLevel = $10;

    printf "%.2f %d %.2f %.2f %d\n", Time, bufferSize, hasThroughput, estimatedBW, videoLevel > "buffer.dat";
  }
}
END {
}