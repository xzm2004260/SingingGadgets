#include <math.h>
#include <memory.h>
#include "Sample.h"
#include "PercussionSampler.h"

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

void PercussionSample(const Sample& sample, float* outBuf, unsigned outBufLen, float sampleRatio)
{
	unsigned chn = sample.m_chn;
	memset(outBuf, 0, sizeof(float)*outBufLen*chn);

	unsigned maxSample = (unsigned)((float)sample.m_wav_length*sampleRatio);
	float mult = 1.0f / sample.m_max_v;

	if (sampleRatio==1.0f)
	{
		for (unsigned j = 0; j < min(outBufLen, maxSample); j++)
		{
			float x2 = (float)j / (float)outBufLen;
			float amplitude = 1.0f - expf((x2 - 1.0f)*10.0f);

			for (unsigned c = 0; c < chn; c++)
			{
				outBuf[j * chn + c] = amplitude*sample.m_wav_samples[j * chn + c] * mult;
			}
		}
	}
	else
	{
		bool interpolation = sampleRatio > 1.0f;
		float inv_sampleRatio = 1.0f / sampleRatio;
		for (unsigned j = 0; j < min(outBufLen, maxSample); j++)
		{
			float x2 = (float)j / (float)outBufLen;
			float amplitude = 1.0f - expf((x2 - 1.0f)*10.0f);

			float wave[2];
			if (interpolation)
			{
				float pos = (float)j *inv_sampleRatio;
				int ipos1 = (int)pos;
				float frac = pos - (float)ipos1;
				int ipos2 = ipos1 + 1;
				if (ipos2 >= (int)sample.m_wav_length) ipos2 = (int)sample.m_wav_length - 1;

				// cubic interpolation
				int ipos0 = ipos1 - 1;
				if (ipos0 < 0) ipos0 = 0;

				int ipos3 = ipos1 + 2;
				if (ipos3 >= (int)sample.m_wav_length) ipos3 = (int)sample.m_wav_length - 1;

				for (unsigned c = 0; c < chn; c++)
				{
					float p0 = sample.m_wav_samples[ipos0*chn + c];
					float p1 = sample.m_wav_samples[ipos1*chn + c];
					float p2 = sample.m_wav_samples[ipos2*chn + c];
					float p3 = sample.m_wav_samples[ipos3*chn + c];

					wave[c] = (-0.5f*p0 + 1.5f*p1 - 1.5f*p2 + 0.5f*p3)*powf(frac, 3.0f) +
						(p0 - 2.5f*p1 + 2.0f*p2 - 0.5f*p3)*powf(frac, 2.0f) +
						(-0.5f*p0 + 0.5f*p2)*frac + p1;

				}

			}
			else
			{
				int ipos1 = (int)ceilf(((float)j - 0.5f)*inv_sampleRatio);
				int ipos2 = (int)floorf(((float)j + 0.5f)*inv_sampleRatio);
				if (ipos1 < 0) ipos1 = 0;
				if (ipos2 >= (int)sample.m_wav_length) ipos2 = (int)sample.m_wav_length - 1;
				int count = ipos2 - ipos1 + 1;

				for (unsigned c = 0; c < chn; c++)
				{
					float sum = 0.0f;
					for (int ipos = ipos1; ipos <= ipos2; ipos++)
					{
						sum += sample.m_wav_samples[ipos*chn + c];
					}
					wave[c] = sum / (float)count;
				}
			}
			for (unsigned c = 0; c < chn; c++)
			{
				outBuf[j*chn + c] = amplitude*wave[c] * mult;
			}
		}

	}

}

