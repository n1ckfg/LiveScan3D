﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace KinectServer
{
    /// <summary>
    /// Data object for kinect configuration (per sensor settings).
    /// </summary>

    public class KinectConfiguration
    {
        public static int bytelength = 22;
        public enum SyncState { Main = 0, Subordinate = 1, Standalone = 2, Unknown = 3 };
        public enum depthResolution { NFOV320Binned = 1, NFOV640Unbinned = 2, WFOV512Binned = 3, WFOV1024Unbinned = 4 };
        public enum colorMode { MJPEG = 0, NV12 = 1, YUV2 = 2, BGRA = 3};
        public enum colorResolution { R720 = 1, R1080 = 2, R1440 = 3, R1536 = 4, R2160 = 5, R3072 = 6 };

        public bool FilterDepthMap;
        public int FilterDepthMapSize;
        public string SerialNumber;
        public byte globalDeviceIndex; //Each Client recieves a unique index from the server 
        public SyncState eSoftwareSyncState; //The sync state as set by the server
        public SyncState eHardwareSyncState; //The sync state that the sync jacks on the device are set for.
        public byte syncOffset; //Increasing number starting a 1, indicating the offset time from the master. Formula to get the actual offset time is SyncOffset * 160 us
        public depthResolution eDepthRes;
        public readonly colorMode eColorMode; //This should only be used to figure out in which state the client is in and is only set by it. 
        public colorResolution eColorRes;

        public KinectConfiguration()
        {
            eDepthRes = depthResolution.NFOV640Unbinned;
            eColorMode = colorMode.BGRA;
            eColorRes = colorResolution.R720;
            eSoftwareSyncState = SyncState.Standalone;
            eHardwareSyncState = SyncState.Unknown;
            syncOffset = 0;
            SerialNumber = "Unknown";
            globalDeviceIndex = 0; // 255 = invalid index
            FilterDepthMap = false;
            FilterDepthMapSize = 0;
        }


        //Matches KinectConfiguration.cpp
        public KinectConfiguration(byte[] bytes)
        { 
            eDepthRes = (depthResolution)bytes[0];
            eColorMode = (colorMode)bytes[1];
            eColorRes = (colorResolution)bytes[2];
            eSoftwareSyncState = (SyncState)bytes[3];
            eHardwareSyncState = (SyncState)bytes[4];
            syncOffset = bytes[5];
            SerialNumber = "";
            for (int i = 6; i < 19; i++)
            {
                SerialNumber += (char)((int)bytes[i]);
            }
            globalDeviceIndex = bytes[19];
            FilterDepthMap = bytes[20] == 0 ? false : true;
            FilterDepthMapSize = bytes[21];
        }

        public byte[] ToBytes()
        {

            byte[] data = new byte[bytelength];
            data[0] = (byte)eDepthRes;
            data[1] = (byte)eColorMode;
            data[2] = (byte)eColorRes;
            data[3] = (byte)eSoftwareSyncState;
            data[4] = (byte)eHardwareSyncState;
            data[5] = syncOffset;

            for(int i = 6; i < 19; i++)
            {
                data[i] = (byte)SerialNumber[i - 6];
            }
            data[19] = globalDeviceIndex;
            data[20] = (byte)(FilterDepthMap ? 1 : 0);
            data[21] = (byte)FilterDepthMapSize;
            return data;
        }
    }
}
