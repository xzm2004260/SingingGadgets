#include <cuda_runtime.h>
#include "SentenceDescriptor.h"
#include "SentenceGeneratorGeneral.h"
#include "SentenceGeneratorCUDA.h"
#include <assert.h>

#include "fft.h"
#include "VoiceUtil.h"
using namespace VoiceUtil;

static float rate = 44100.0f;

inline void Clamp01(float& v)
{
	if (v < 0.0f) v = 0.0f;
	else if (v > 1.0f) v = 1.0f;
}



template <class T>
class CUDAVector
{
public:
	CUDAVector()
	{
		count = 0;
		d_data = nullptr;
	}

	~CUDAVector()
	{
		Free();
	}

	unsigned Count() const
	{
		return count;
	}

	T* Pointer()
	{
		return d_data;
	}

	const T* ConstPointer() const
	{
		return d_data;
	}

	operator T*()
	{
		return d_data;
	}

	operator const T*()
	{
		return d_data;
	}

	void Free()
	{
		if (d_data != nullptr)
		{
			cudaFree(d_data);
			d_data = nullptr;
		}
		count = 0;
	}

	void Allocate(unsigned count)
	{
		Free();
		this->count = count;
		if (count>0)
			cudaMalloc(&d_data, sizeof(T)*count);
	}

	const CUDAVector& operator = (const std::vector<T>& cpuVec)
	{
		Free();
		Allocate((unsigned)cpuVec.size());
		if (count > 0)
		{
			cudaMemcpy(d_data, cpuVec.data(), sizeof(T)*count, cudaMemcpyHostToDevice);
		}

		return *this;
	}

	void ToCPU(std::vector<T>& cpuVec) const
	{
		cpuVec.resize(count);
		cudaMemcpy(cpuVec.data(), d_data, sizeof(T)*count, cudaMemcpyDeviceToHost);
	}

	void Update(const std::vector<T>& cpuVec)
	{
		assert(count == (unsigned)cpuVec.size());
		if (count > 0)
		{
			cudaMemcpy(d_data, cpuVec.data(), sizeof(T)*count, cudaMemcpyHostToDevice);
		}
	}

	void MakeLeaky()
	{
		count = 0;
		d_data = nullptr;
	}

private:
	unsigned count;
	T* d_data;

};

template<class T>
class Leaky_T : public T
{
public:
	using T::MakeLeaky;
	using T::operator =;
	Leaky_T(){}
	~Leaky_T()
	{
		MakeLeaky();
	}
};

template <class T_GPU, class T_CPU>
class CUDAImagedVector
{
public:
	CUDAImagedVector()
	{

	}
	~CUDAImagedVector()
	{
		Free();
	}

	unsigned Count() const
	{
		return m_vec.Count();
	}

	T_GPU* Pointer()
	{
		return m_vec.Pointer();
	}

	const T_GPU* ConstPointer() const
	{
		return m_vec.ConstPointer();
	}

	operator T_GPU*()
	{
		return m_vec.Pointer();
	}

	operator const T_GPU*() const
	{
		return m_vec.ConstPointer();
	}

	void Free()
	{
		if (m_vec.Count() > 0)
		{
			std::vector<T_GPU> temp;
			m_vec.ToCPU(temp);
		}
		m_vec.Free();
	}

	const CUDAImagedVector& operator = (const std::vector<T_CPU>& cpuVecs)
	{
		Free();
		m_vec.Allocate((unsigned)cpuVecs.size());
		if (m_vec.Count() > 0)
		{
			std::vector<Leaky_T<T_GPU>> temp;
			temp.resize(cpuVecs.size());
			for (unsigned i = 0; i < (unsigned)cpuVecs.size(); i++)
				temp[i] = cpuVecs[i];
			cudaMemcpy(m_vec.Pointer(), temp.data(), sizeof(T_GPU)*m_vec.Count(), cudaMemcpyHostToDevice);
		}
		return *this;
	}

	void ToCPU(std::vector<T_CPU>& cpuVecs) const
	{
		cpuVecs.resize(m_vec.Count());
		if (m_vec.Count() > 0)
		{
			std::vector<Leaky_T<T_GPU>> temp;
			temp.resize(m_vec.Count());
			cudaMemcpy(temp.data(), m_vec.ConstPointer(), sizeof(T_GPU)*m_vec.Count(), cudaMemcpyDeviceToHost);
			for (unsigned i = 0; i < (unsigned)cpuVecs.size(); i++)
				temp[i].ToCPU(cpuVecs[i]);
		}
	}

	void Update(const std::vector<T_CPU>& cpuVecs)
	{
		assert(m_vec.Count() == (unsigned)cpuVecs.size());
		if (m_vec.Count() > 0)
		{
			std::vector<Leaky_T<T_GPU>> temp;
			temp.resize(m_vec.Count());
			cudaMemcpy(temp.data(), m_vec.ConstPointer(), sizeof(T_GPU)*m_vec.Count(), cudaMemcpyDeviceToHost);
			for (unsigned i = 0; i < (unsigned)cpuVecs.size(); i++)
				temp[i].Update(cpuVecs[i]);
			cudaMemcpy(m_vec.Pointer(), temp.data(), sizeof(T_GPU)*m_vec.Count(), cudaMemcpyHostToDevice);
		}
	}

	void MakeLeaky()
	{
		m_vec.MakeLeaky();
	}

protected:
	CUDAVector<T_GPU> m_vec;
};

template <class T>
class CUDALevel2Vector : public CUDAImagedVector<CUDAVector<T>, std::vector<T> >
{
public:
	using CUDAImagedVector<CUDAVector<T>, std::vector<T> >::Free;
	using CUDAImagedVector<CUDAVector<T>, std::vector<T> >::m_vec;
	void Allocate(const std::vector<unsigned>& counts)
	{
		Free();
		m_vec.Allocate((unsigned)counts.size());
		if (m_vec.Count() > 0)
		{
			std::vector<Leaky_T<CUDAVector<T>>> temp;
			temp.resize(counts.size());
			for (unsigned i = 0; i < (unsigned)counts.size(); i++)
			{
				temp[i].Allocate(counts[i]);
			}
			cudaMemcpy(m_vec.Pointer(), temp.data(), sizeof(CUDAVector<T>)*m_vec.Count(), cudaMemcpyHostToDevice);
		}
	}
	using CUDAImagedVector<CUDAVector<T>, std::vector<T>>::operator =;
};

class CUDASrcBuf : public CUDAVector<float>
{
public:
	const CUDASrcBuf&  operator = (const Buffer& cpuVec)
	{
		CUDAVector<float>::operator=(cpuVec.m_data);
		return *this;
	}

	void ToCPU(Buffer& cpuVec) const
	{
		CUDAVector<float>::ToCPU(cpuVec.m_data);
	}

	void Update(const Buffer& cpuVec)
	{
		CUDAVector<float>::Update(cpuVec.m_data);
	}
};

typedef CUDAImagedVector<CUDASrcBuf, Buffer> CUDASrcBufList;


struct SrcSampleInfo
{
	unsigned srcPos;
	float srcSampleFreq;
	float dstPos;
	int isVowel;
};

typedef std::vector<SrcSampleInfo> SrcPieceInfo;
typedef CUDAVector<SrcSampleInfo> CUDASrcPieceInfo;
typedef CUDALevel2Vector<SrcSampleInfo> CUDASrcPieceInfoList;

struct Job
{
	unsigned pieceId;
	unsigned jobOfPiece;
};

struct DstPieceInfo
{
	float minSampleFreq;
	unsigned uSumLen;
	float tempLen;
	unsigned uTempLen;
	float fTmpWinCenter0;
};


struct SynthJobInfo
{
	unsigned pieceId;
	unsigned jobOfPiece;
	unsigned srcPieceId0;
	unsigned srcPieceId1;
	float k_srcPiece;
	unsigned paramId00;
	unsigned paramId10;
	float k0;
	float k1;
	float destHalfWinLen;
};


struct CUDATempBuffer
{
	unsigned count;
	float *d_data;
};

void h_GetMaxVoiced(CUDASrcBufList cuSrcBufs, CUDASrcPieceInfoList pieceInfoList,
	CUDALevel2Vector<unsigned> cuMaxVoicedLists, CUDAVector<Job> jobMap, unsigned BufSize);

void h_AnalyzeInput(CUDASrcBufList cuSrcBufs, CUDASrcPieceInfoList pieceInfoList, unsigned halfWinLen,
	unsigned specLen, CUDALevel2Vector<float> cuHarmWindows, CUDALevel2Vector<float> cuNoiseSpecs,
	CUDALevel2Vector<unsigned> cuMaxVoicedLists, CUDAVector<Job> jobMap, unsigned BufSize);

void h_Synthesis(CUDASrcPieceInfoList cuSrcPieceInfos, unsigned halfWinLen, unsigned specLen,
	CUDALevel2Vector<float> cuHarmWindows, CUDALevel2Vector<float> cuNoiseSpecs,
	CUDAVector<DstPieceInfo> cuDstPieceInfos, CUDAVector<CUDATempBuffer> cuTmpBufs1, CUDAVector<CUDATempBuffer> cuTmpBufs2,
	CUDAVector<float> cuRandPhase, CUDAVector<SynthJobInfo> cuSynthJobs, unsigned BufSize);

void h_Merge2Bufs(unsigned uSumLen, float *d_destBuf1, float *d_destBuf2);

void GenerateSentenceCUDA(const SentenceDescriptor* desc, float* outBuf, unsigned outBufLen)
{
	std::vector<Buffer> SrcBuffers;
	const std::vector<Piece>& pieces = desc->pieces;

	unsigned numSrcPieces = (unsigned)pieces.size();

	SrcBuffers.resize(numSrcPieces);
	for (size_t i = 0; i < numSrcPieces; i++)
	{
		const Piece& piece = pieces[i];

		int srcStart = (int)(piece.srcMap[0].srcPos*0.001f*rate);
		int srcEnd = (int)ceilf(piece.srcMap[piece.srcMap.size() - 1].srcPos*0.001f*rate);

		SrcBuffers[i].m_sampleRate = (unsigned)rate;
		RegulateSource(piece.src.wav.buf, piece.src.wav.len, SrcBuffers[i], srcStart, srcEnd);
	}

	CUDASrcBufList cuSourceBufs;
	cuSourceBufs = SrcBuffers;

	std::vector<SrcPieceInfo> SrcPieceInfos;
	SrcPieceInfos.resize(numSrcPieces);

	float max_srcHalfWinWidth = 0.0f;
	float max_freqDetectHalfWinWidth = 0.0f;

	std::vector<Job> jobMap;
	std::vector<unsigned> countMaxVoiceds;
	countMaxVoiceds.resize(numSrcPieces);

	for (size_t i = 0; i < numSrcPieces; i++)
	{
		const Piece& piece = pieces[i];
		SrcPieceInfo& srcPieceInfo = SrcPieceInfos[i];

		int srcStart = (int)(piece.srcMap[0].srcPos*0.001f*rate);
		int srcEnd = (int)ceilf(piece.srcMap[piece.srcMap.size() - 1].srcPos*0.001f*rate);

		float fPeriodCount = 0.0f;
		unsigned i_srcMap = 0;
		unsigned lastmaxVoiced = 0;

		for (int srcPos = srcStart; srcPos < srcEnd; srcPos++)
		{
			float fsrcPos = (float)srcPos / rate*1000.0f;
			while (i_srcMap + 1 < piece.srcMap.size() && fsrcPos >= piece.srcMap[i_srcMap + 1].srcPos)
				i_srcMap++;

			int isVowel = piece.srcMap[i_srcMap].isVowel;

			float k_srcMap = (fsrcPos - piece.srcMap[i_srcMap].srcPos) / (piece.srcMap[i_srcMap + 1].srcPos - piece.srcMap[i_srcMap].srcPos);
			Clamp01(k_srcMap);
			float fdstPos = piece.srcMap[i_srcMap].dstPos*(1.0f - k_srcMap) + piece.srcMap[i_srcMap + 1].dstPos*k_srcMap;
			float dstPos = fdstPos*0.001f*rate;

			float srcSampleFreq;
			float srcFreqPos = (float)srcPos / (float)piece.src.frq.interval;
			unsigned uSrcFreqPos = (unsigned)srcFreqPos;
			float fracSrcFreqPos = srcFreqPos - (float)uSrcFreqPos;

			float freq1 = (float)piece.src.frq.data[uSrcFreqPos].freq;
			if (freq1 <= 55.0f) freq1 = (float)piece.src.frq.key;

			float freq2 = (float)piece.src.frq.data[uSrcFreqPos + 1].freq;
			if (freq2 <= 55.0f) freq2 = (float)piece.src.frq.key;

			float sampleFreq1 = freq1 / rate;
			float sampleFreq2 = freq2 / rate;

			srcSampleFreq = sampleFreq1*(1.0f - fracSrcFreqPos) + sampleFreq2*fracSrcFreqPos;

			unsigned paramId = (unsigned)fPeriodCount;
			if (paramId >= srcPieceInfo.size())
			{
				SrcSampleInfo sl;
				sl.srcSampleFreq = srcSampleFreq;
				sl.srcPos = srcPos - srcStart;
				sl.isVowel = isVowel;
				sl.dstPos = dstPos;

				srcPieceInfo.push_back(sl);

				float srcHalfWinWidth = 1.0f / srcSampleFreq;
				if (max_srcHalfWinWidth < srcHalfWinWidth)
					max_srcHalfWinWidth = srcHalfWinWidth;

				if (isVowel<2)
				{
					float halfWinlen = 3.0f / srcSampleFreq;
					if (halfWinlen > max_freqDetectHalfWinWidth)
						max_freqDetectHalfWinWidth = halfWinlen;

					Job job;
					job.pieceId = (unsigned)i;
					job.jobOfPiece = (unsigned)srcPieceInfo.size() - 1;
					jobMap.push_back(job);
				}
			}
			fPeriodCount += srcSampleFreq;
		}

		countMaxVoiceds[i] = (unsigned)srcPieceInfo.size();
	}

	CUDASrcPieceInfoList cuSrcPieceInfos;
	cuSrcPieceInfos = SrcPieceInfos;

	CUDAVector<Job> cuJobMap;
	cuJobMap = jobMap;

	CUDALevel2Vector<unsigned> cuMaxVoicedLists;
	cuMaxVoicedLists.Allocate(countMaxVoiceds);

	unsigned cuHalfWinLen = (unsigned)ceilf(max_srcHalfWinWidth);
	unsigned cuSpecLen = (unsigned)ceilf(max_srcHalfWinWidth*0.5f);

	unsigned fftLen = 1;
	while ((float)fftLen < max_freqDetectHalfWinWidth)
		fftLen <<= 1;
	unsigned BufSize = (unsigned)ceilf(max_freqDetectHalfWinWidth) * 2 + fftLen * 2;

	h_GetMaxVoiced(cuSourceBufs, cuSrcPieceInfos, cuMaxVoicedLists, cuJobMap, BufSize);

	std::vector<std::vector<unsigned>> h_maxVoicedLists;
	cuMaxVoicedLists.ToCPU(h_maxVoicedLists);

	for (unsigned i = 0; i < h_maxVoicedLists.size(); i++)
	{
		std::vector<unsigned>& sublist = h_maxVoicedLists[i];
		SrcPieceInfo& srcPieceInfo = SrcPieceInfos[i];

		unsigned lastmaxVoiced = 0;
		for (unsigned j = 0; j < srcPieceInfo.size(); j++)
		{
			if (srcPieceInfo[j].isVowel < 2)
			{
				if (srcPieceInfo[j].isVowel > 0 && sublist[j] < lastmaxVoiced)
				{
					sublist[j] = lastmaxVoiced;
				}
				lastmaxVoiced = sublist[j];
			}
		}
	}
	cuMaxVoicedLists.Update(h_maxVoicedLists);

	std::vector<unsigned> cuTotalHalfWinLen;
	cuTotalHalfWinLen.resize(numSrcPieces);
	std::vector<unsigned> cuTotalSpecLen;
	cuTotalSpecLen.resize(numSrcPieces);

	for (unsigned i = 0; i < numSrcPieces; i++)
	{
		cuTotalHalfWinLen[i] = cuHalfWinLen*(unsigned)SrcPieceInfos[i].size();
		cuTotalSpecLen[i] = cuSpecLen*(unsigned)SrcPieceInfos[i].size();
	}

	CUDALevel2Vector<float> cuHarmWindows;
	cuHarmWindows.Allocate(cuTotalHalfWinLen);
	CUDALevel2Vector<float> cuNoiseSpecs;
	cuNoiseSpecs.Allocate(cuTotalSpecLen);

	fftLen = 1;
	while (fftLen < cuHalfWinLen)
		fftLen <<= 1;

	BufSize = cuHalfWinLen * 2 + fftLen * 2;

	jobMap.clear();
	for (unsigned i = 0; i < numSrcPieces; i++)
	{
		for (unsigned j = 0; j < (unsigned)SrcPieceInfos[i].size(); j++)
		{
			Job job;
			job.pieceId = i;
			job.jobOfPiece = j;
			jobMap.push_back(job);
		}
	}
	cuJobMap = jobMap;

	h_AnalyzeInput(cuSourceBufs, cuSrcPieceInfos, cuHalfWinLen, cuSpecLen, cuHarmWindows, cuNoiseSpecs, cuMaxVoicedLists, cuJobMap, BufSize);

	float* freqMap = new float[outBufLen];
	std::vector<unsigned> bounds;

	PreprocessFreqMap(desc, outBufLen, freqMap, bounds);

	unsigned numDstPieces = (unsigned)bounds.size() -1;
	std::vector<const float*> freqMaps;
	freqMaps.resize(numDstPieces);
	std::vector<std::vector<float>> stretchingMaps;
	stretchingMaps.resize(numDstPieces);
	std::vector<DstPieceInfo> DstPieceInfos;
	DstPieceInfos.resize(numDstPieces);

	unsigned sumTmpBufLen = 0;
	for (unsigned i = 0; i < numDstPieces; i++)
	{
		DstPieceInfo& dstPieceInfo = DstPieceInfos[i];
		dstPieceInfo.uSumLen = bounds[i + 1] - bounds[i];
		freqMaps[i] = freqMap + bounds[i];
		dstPieceInfo.minSampleFreq = FLT_MAX;
		for (unsigned pos = 0; pos < dstPieceInfo.uSumLen; pos++)
		{
			float sampleFreq = freqMaps[i][pos];
			if (sampleFreq < dstPieceInfo.minSampleFreq) dstPieceInfo.minSampleFreq = sampleFreq;
		}
		stretchingMaps[i].resize(dstPieceInfo.uSumLen);

		float pos_tmpBuf = 0.0f;
		for (unsigned pos = 0; pos < dstPieceInfo.uSumLen; pos++)
		{
			float sampleFreq;
			sampleFreq = freqMaps[i][pos];

			float speed = sampleFreq / dstPieceInfo.minSampleFreq;
			pos_tmpBuf += speed;
			stretchingMaps[i][pos] = pos_tmpBuf;
		}
		dstPieceInfo.tempLen = stretchingMaps[i][dstPieceInfo.uSumLen - 1];
		dstPieceInfo.uTempLen = (unsigned)ceilf(dstPieceInfo.tempLen);

		sumTmpBufLen += dstPieceInfo.uTempLen;
	}

	std::vector<SynthJobInfo> SynthJobs;

	float phase = 0.0f;
	unsigned i_pieceMap = 0;

	const std::vector<GeneralCtrlPnt>& piece_map = desc->piece_map;
	const std::vector<GeneralCtrlPnt>& volume_map = desc->volume_map;

	unsigned maxRandPhaseLen = 0;
	float maxtempHalfWinLen = 0.0f;

	for (unsigned i = 0; i < numDstPieces; i++)
	{
		DstPieceInfo& dstPieceInfo = DstPieceInfos[i];
		float tempHalfWinLen = 1.0f / dstPieceInfo.minSampleFreq;
		if (tempHalfWinLen > maxtempHalfWinLen)
			maxtempHalfWinLen = tempHalfWinLen;

		unsigned pos_local = 0;
		while (phase > -1.0f) phase -= 1.0f;

		float tempLen = dstPieceInfo.tempLen;
		unsigned uSumLen = dstPieceInfo.uSumLen;

		const float* pFreqMap = freqMaps[i];
		std::vector<float>& stretchingMap = stretchingMaps[i];

		float fTmpWinCenter = phase*tempHalfWinLen;
		dstPieceInfo.fTmpWinCenter0 = fTmpWinCenter;
		unsigned jobOfPiece = 0;

		while (fTmpWinCenter - tempHalfWinLen <= tempLen)
		{
			SynthJobInfo synthJob;
			synthJob.pieceId = i;
			synthJob.jobOfPiece = jobOfPiece;

			while (fTmpWinCenter > stretchingMap[pos_local] && pos_local < uSumLen - 1) pos_local++;
			unsigned pos_global = pos_local + bounds[i];
			float f_pos_global = (float)pos_global / rate*1000.0f;

			float destSampleFreq;
			destSampleFreq = pFreqMap[pos_local];
			float destHalfWinLen = 1.0f / destSampleFreq;
			synthJob.destHalfWinLen = destHalfWinLen;

			while (i_pieceMap + 1 < piece_map.size() && f_pos_global >= piece_map[i_pieceMap + 1].dstPos)
				i_pieceMap++;

			float k_piece = (f_pos_global - piece_map[i_pieceMap].dstPos) / (piece_map[i_pieceMap + 1].dstPos - piece_map[i_pieceMap].dstPos);
			Clamp01(k_piece);
			float fPieceId = piece_map[i_pieceMap].value* (1.0f - k_piece) + piece_map[i_pieceMap + 1].value*k_piece;

			unsigned pieceId0 = (unsigned)fPieceId;
			float pieceId_frac = fPieceId - (float)pieceId0;
			unsigned pieceId1 = (unsigned)fPieceId + 1;
			if (pieceId0 >= (unsigned)pieces.size()) pieceId0 = (unsigned)pieces.size() - 1;
			if (pieceId_frac == 0.0f || pieceId1 >= (unsigned)pieces.size())
			{
				pieceId1 = pieceId0;
				pieceId_frac = 0.0f;
			}

			synthJob.srcPieceId0 = pieceId0;
			synthJob.srcPieceId1 = pieceId1;
			synthJob.k_srcPiece = pieceId_frac;

			std::vector<SrcSampleInfo>& SampleLocations0 = SrcPieceInfos[pieceId0];
			std::vector<SrcSampleInfo>& SampleLocations1 = SrcPieceInfos[pieceId1];

			unsigned paramId00 = 0;
			unsigned paramId01 = 1;
			unsigned paramId10 = 0;
			unsigned paramId11 = 1;

			while (paramId01<SampleLocations0.size() && SampleLocations0[paramId01].dstPos <(float)pos_global)
			{
				paramId00++;
				paramId01 = paramId00 + 1;
			}
			if (paramId01 == SampleLocations0.size()) paramId01 = paramId00;
			synthJob.paramId00 = paramId00;
			synthJob.paramId10 = 0;

			if (pieceId1 > pieceId0)
			{
				while (paramId11<SampleLocations1.size() && SampleLocations1[paramId11].dstPos <(float)pos_global)
				{
					paramId10++;
					paramId11 = paramId10 + 1;
				}
				if (paramId11 == SampleLocations1.size()) paramId11 = paramId10;
				synthJob.paramId10 = paramId10;
			}

			SrcSampleInfo& sl00 = SampleLocations0[paramId00];
			SrcSampleInfo& sl01 = SampleLocations0[paramId01];

			float k0;
			if ((float)pos_global >= sl01.dstPos) k0 = 1.0f;
			else if ((float)pos_global <= sl00.dstPos) k0 = 0.0f;
			else
			{
				k0 = ((float)pos_global - sl00.dstPos) / (sl01.dstPos - sl00.dstPos);
			}
			synthJob.k0 = k0;
			synthJob.k1 = 0.0f;

			if (pieceId1 > pieceId0)
			{
				SrcSampleInfo& sl10 = SampleLocations1[paramId10];
				SrcSampleInfo& sl11 = SampleLocations1[paramId11];

				float k1;
				if ((float)pos_global >= sl11.dstPos) k1 = 1.0f;
				else if ((float)pos_global <= sl10.dstPos) k1 = 0.0f;
				else
				{
					k1 = ((float)pos_global - sl10.dstPos) / (sl11.dstPos - sl10.dstPos);
				}
				synthJob.k1 = k1;
			}
			SynthJobs.push_back(synthJob);

			jobOfPiece++;
			fTmpWinCenter += tempHalfWinLen;
		}

		unsigned uSpecLen = (unsigned)ceilf(tempHalfWinLen*0.5f);
		unsigned randPhaseLen = uSpecLen*jobOfPiece;
		if (randPhaseLen > maxRandPhaseLen)
			maxRandPhaseLen = randPhaseLen;

		phase = (fTmpWinCenter - tempLen) / tempHalfWinLen;

	}

	CUDAVector<DstPieceInfo> cuDstPieceInfos;
	cuDstPieceInfos = DstPieceInfos;

	CUDAVector<SynthJobInfo> cuSynthJobs;
	cuSynthJobs = SynthJobs;

	CUDAVector<float> cuSumTmpBuf1;
	CUDAVector<float> cuSumTmpBuf2;
	cuSumTmpBuf1.Allocate(sumTmpBufLen);
	cuSumTmpBuf2.Allocate(sumTmpBufLen);

	cudaMemset(cuSumTmpBuf1, 0, sizeof(float)*sumTmpBufLen);
	cudaMemset(cuSumTmpBuf2, 0, sizeof(float)*sumTmpBufLen);

	std::vector<CUDATempBuffer> tmpBufs1;
	tmpBufs1.resize(numDstPieces);
	std::vector<CUDATempBuffer> tmpBufs2;
	tmpBufs2.resize(numDstPieces);

	float *pTmpBuf1 = cuSumTmpBuf1.Pointer();
	float *pTmpBuf2 = cuSumTmpBuf2.Pointer();
	for (unsigned i = 0; i < numDstPieces; i++)
	{
		unsigned count = DstPieceInfos[i].uTempLen;
		tmpBufs1[i].count = count;
		tmpBufs1[i].d_data = pTmpBuf1;
		tmpBufs2[i].count = count;
		tmpBufs2[i].d_data = pTmpBuf2;
		pTmpBuf1 += count;
		pTmpBuf2 += count;
	}

	CUDAVector<CUDATempBuffer> cuTmpBufs1;
	cuTmpBufs1 = tmpBufs1;
	CUDAVector<CUDATempBuffer> cuTmpBufs2;
	cuTmpBufs2 = tmpBufs2;

	std::vector<float> randPhase;
	randPhase.resize(maxRandPhaseLen);

	for (unsigned i = 0; i < maxRandPhaseLen; i++)
		randPhase[i] = rand01();

	CUDAVector<float> cuRandPhase;
	cuRandPhase = randPhase;

	fftLen = 1;
	while ((float)fftLen < maxtempHalfWinLen)
		fftLen <<= 1;

	BufSize = fftLen * 4;

	h_Synthesis(cuSrcPieceInfos, cuHalfWinLen, cuSpecLen, cuHarmWindows, cuNoiseSpecs, 	
		cuDstPieceInfos, cuTmpBufs1, cuTmpBufs2, cuRandPhase, cuSynthJobs, BufSize);

	h_Merge2Bufs(sumTmpBufLen, cuSumTmpBuf1, cuSumTmpBuf2);

	std::vector<float> sumTmpBuf;
	cuSumTmpBuf1.ToCPU(sumTmpBuf);

	float* pTmpBuf = &sumTmpBuf[0];
	float* pDstBuf = outBuf;
	unsigned i_volumeMap = 0;
	for (unsigned i = 0; i < numDstPieces; i++)
	{
		unsigned uSumLen = DstPieceInfos[i].uSumLen;
		float *stretchingMap = &stretchingMaps[i][0];
		const float *pFreqMap = freqMaps[i];
		float minSampleFreq = DstPieceInfos[i].minSampleFreq;
		unsigned uTempLen = DstPieceInfos[i].uTempLen;

		for (unsigned pos = 0; pos < uSumLen; pos++)
		{
			unsigned pos_global = pos + bounds[i];
			float f_pos_global = (float)pos_global / rate*1000.0f;
			while (i_volumeMap + 1 < volume_map.size() && f_pos_global >= volume_map[i_volumeMap + 1].dstPos)
				i_volumeMap++;
			float k_volume = (f_pos_global - volume_map[i_volumeMap].dstPos) / (volume_map[i_volumeMap + 1].dstPos - volume_map[i_volumeMap].dstPos);
			Clamp01(k_volume);
			float volume = volume_map[i_volumeMap].value* (1.0f - k_volume) + volume_map[i_volumeMap + 1].value*k_volume;

			float pos_tmpBuf = stretchingMap[pos];
			float sampleFreq;
			sampleFreq = pFreqMap[pos];

			float speed = sampleFreq / minSampleFreq;

			int ipos1 = (int)ceilf(pos_tmpBuf - speed*0.5f);
			int ipos2 = (int)floorf(pos_tmpBuf + speed*0.5f);

			if (ipos1 >= (int)uTempLen) ipos1 = (int)uTempLen - 1;
			if (ipos2 >= (int)uTempLen) ipos2 = (int)uTempLen - 1;

			float sum = 0.0f;
			for (int ipos = ipos1; ipos <= ipos2; ipos++)
			{
				if (ipos >= 0)
					sum += pTmpBuf[ipos];
			}
			float value = sum / (float)(ipos2 - ipos1 + 1);
			pDstBuf[pos] = value*volume;
		}
		pTmpBuf += uTempLen;
		pDstBuf += uSumLen;
	}


	delete[] freqMap;
}
