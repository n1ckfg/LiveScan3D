#include "frameFileWriterReader.h"
#include <ctime>

#include <fstream>
#include <assert.h>
//#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING //Otherwise VS yells




namespace fs = std::filesystem;


FrameFileWriterReader::FrameFileWriterReader()
{

}

void FrameFileWriterReader::closeFileIfOpened()
{
	if (m_pFileHandle == nullptr)
		return;

	fclose(m_pFileHandle);
	m_pFileHandle = nullptr; 
	m_bFileOpenedForReading = false;
	m_bFileOpenedForWriting = false;
}

void FrameFileWriterReader::resetTimer()
{
	recording_start_time = std::chrono::steady_clock::now();
}

int FrameFileWriterReader::getRecordingTimeMilliseconds()
{
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds >(end - recording_start_time).count());
}

void FrameFileWriterReader::openCurrentFileForReading()
{
	closeFileIfOpened();

	m_pFileHandle = fopen(m_sFilename.c_str(), "rb");

	m_bFileOpenedForReading = true;
	m_bFileOpenedForWriting = false;
}

void FrameFileWriterReader::openNewFileForWriting(int deviceID)
{
	closeFileIfOpened();

	char filename[1024];
	time_t t = time(0);
	struct tm * now = localtime(&t);
	sprintf(filename, "recording_%01d_%04d_%02d_%02d_%02d_%02d.bin", deviceID, now->tm_year + 1900, now->tm_mon + 1, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);
	m_sFilename = m_sFrameRecordingsDir;
	m_sFilename +=	filename; 
	m_pFileHandle = fopen(m_sFilename.c_str(), "wb");

	m_bFileOpenedForReading = false;
	m_bFileOpenedForWriting = true;

	resetTimer();
}

bool FrameFileWriterReader::readFrame(Point3s* &outPoints, RGB* &outColors, int &outPointsSize)
{
	std::cout << "Reading Pointcloud Frame" << std::endl;

	if (!m_bFileOpenedForReading)
		openCurrentFileForReading();

	FILE *f = m_pFileHandle;
	int nPoints, timestamp; 
	char tmp[1024]; 
	int nread = fscanf_s(f, "%s %d %s %d", tmp, 1024, &nPoints, tmp, 1024, &timestamp);

	if (nread < 4)
		return false;

	if (nPoints == 0)
		return true;

	outPointsSize = nPoints;

	fgetc(f);		//  '\n'
	outPoints = new Point3s[nPoints];
	outColors = new RGB[nPoints];

	fread((void*)outPoints, sizeof(outPoints[0]), nPoints, f);
	fread((void*)outColors, sizeof(outColors[0]), nPoints, f);
	fgetc(f);		// '\n'
	return true;

}


void FrameFileWriterReader::writeFrame(Point3s* points, int pointsSize, RGB* colors, uint64_t timestamp, int deviceID)
{
	std::cout << "Writing pointcloud frame" << std::endl;

	if (!m_bFileOpenedForWriting)
		openNewFileForWriting(deviceID);

	FILE *f = m_pFileHandle;

	int nPoints = static_cast<int>(pointsSize);
	
	//The Timestamp is generated by the Kinect instead of the system. If temporal Sync is enabled, Master and Subordinate have a synced timestamp
	fprintf(f, "n_points= %d\nframe_timestamp= %d\n", nPoints, timestamp);

	//std::cout << "Writing Frame, Timestamp: " << timestamp << std::endl;

	if (nPoints > 0)
	{
		fwrite(points, sizeof(points[0]), nPoints, f);
		fwrite(colors, sizeof(colors[0]), nPoints, f);
	}
	fprintf(f, "\n");
}


/// <summary>
/// Given a dir in which all clients/server should store their recordings in, creates a client-specific dir.
/// Also creates all parent directorys neccessary.
/// </summary>
/// <returns> Returns true on success, returns false if there are errors during file path creation.</returns>
bool FrameFileWriterReader::CreateRecordDirectory(std::string newDirToCreate, const int deviceID)
{
	std::cout << "Creating directory: " << newDirToCreate << std::endl;

	fs::path generalOutputPath = fs::current_path();
	generalOutputPath /= "out\\"; //The directory in which all recordings are stored

	if (!CreateDir(generalOutputPath))
		return false;

	fs::path takeDir = generalOutputPath; 
	takeDir /= newDirToCreate; //The take dir in which the recordings of this take are saved

	if (!CreateDir(takeDir))
		return false;

	fs::path clientTakeDir = takeDir;
	std::string deviceIDDir = "client_";
	deviceIDDir += std::to_string(deviceID);
	deviceIDDir += "\\";
	clientTakeDir /= deviceIDDir; //The directory in which we store the recordings of this specific client

    if (!CreateDir(clientTakeDir))
			return false;

	m_sFrameRecordingsDir = clientTakeDir.string();

	std::cout << "Successfully created directory: " << m_sFrameRecordingsDir << std::endl;

	return true;
}

void FrameFileWriterReader::WriteColorJPGFile(void* buffer, size_t bufferSize, int frameIndex)
{
	std::cout << "Writing Color JPEG File with index: " << frameIndex << std::endl;

	std::string colorfilePath = "Color_";
	colorfilePath += std::to_string(frameIndex);
	colorfilePath += ".jpg";

	std::string filePath = m_sFrameRecordingsDir;
	filePath += colorfilePath;

	assert(buffer != NULL);

	std::ofstream hFile;
	hFile.open(filePath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	if (hFile.is_open())
	{
		hFile.write((char*)buffer, static_cast<std::streamsize>(bufferSize));
		hFile.close();
	}
}

void FrameFileWriterReader::WriteCalibrationJSON(int deviceIndex, const std::vector<uint8_t> calibration_buffer, const size_t calibration_size)
{
	std::cout << "Writing Calibration JSON" << std::endl;

	std::string filename = m_sFrameRecordingsDir;
	filename += "Intrinsics_Calib_";
	filename += std::to_string(deviceIndex);
	filename += ".json";

	std::ofstream file(filename, std::ofstream::binary);
	file.write(reinterpret_cast<const char*>(&calibration_buffer[0]), (long)calibration_size);
	file.close();
}

void FrameFileWriterReader::WriteDepthTiffFile(const k4a_image_t &im, int frameIndex)
{
	std::cout << "Writing Depth Tiff File with index: " << frameIndex << std::endl;

	std::string depthFilePath = "Depth_";
	depthFilePath += std::to_string(frameIndex);
	depthFilePath += ".tiff";

	std::string filePath = m_sFrameRecordingsDir;
	filePath += depthFilePath;

	cv::Mat depthMat = cv::Mat(k4a_image_get_height_pixels(im), k4a_image_get_width_pixels(im), CV_16U, k4a_image_get_buffer(im), static_cast<size_t>(k4a_image_get_stride_bytes(im)));

	bool result = false;

	try
	{
		cv::imwrite(filePath, depthMat);
	}
	catch (const cv::Exception& ex)
	{
		fprintf(stderr, "Exception converting image to Tiff format: %s\n", ex.what());
	}
}

void FrameFileWriterReader::WriteTimestampLog(std::vector<int> frames, std::vector<uint64_t> timestamps, int deviceIndex) 
{
	std::cout << "Writing new Timestamp log" << std::endl;

	std::string filename = m_sFrameRecordingsDir;
	filename += "Timestamps_Client";
	filename += std::to_string(deviceIndex);
	filename += ".txt";

	if (frames.size() < 1)
		return;

	std::ofstream file;
	file.open(filename);
	
	for (int i = 0; i < frames.size(); i++)
	{
		file << frames[i] << "\t" << timestamps[i] << "\n";
	}

	file.close();
}

/// <summary>
/// Creates a directory. Should be given an absolute path
/// </summary>
/// <param name="path"></param>
/// <returns>Returns true if the directory exists, false when an error has occured during creation</returns>
bool FrameFileWriterReader::CreateDir(const fs::path dirToCreate)
{
	if (!fs::exists(dirToCreate))
	{
		try
		{
			if (!fs::create_directory(dirToCreate))
				return false; //Could not create dir

			else
				return true;
		}

		catch (fs::filesystem_error const& ex)
		{
			return false; //Error during dir creation
		}
	}

	else
		return true;
}

bool FrameFileWriterReader::DirExists(std::string path) 
{
	fs::path pathToCheck = path;

	return fs::exists(pathToCheck);
}

FrameFileWriterReader::~FrameFileWriterReader()
{
	closeFileIfOpened();
}
