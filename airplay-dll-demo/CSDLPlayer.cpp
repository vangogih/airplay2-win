#include "CSDLPlayer.h"
#include <stdio.h>
#include "CAutoLock.h"

/* This function may run in a separate event thread */
int FilterEvents(const SDL_Event* event) {
// 	static int boycott = 1;
// 
// 	/* This quit event signals the closing of the window */
// 	if ((event->type == SDL_QUIT) && boycott) {
// 		printf("Quit event filtered out -- try again.\n");
// 		boycott = 0;
// 		return(0);
// 	}
// 	if (event->type == SDL_MOUSEMOTION) {
// 		printf("Mouse moved to (%d,%d)\n",
// 			event->motion.x, event->motion.y);
// 		return(0);    /* Drop it, we've handled it */
// 	}
	return(1);
}

CSDLPlayer::CSDLPlayer()
	: m_surface(NULL)
	, m_yuv(NULL)
	, m_bAudioInited(false)
	, m_bDumpAudio(false)
	, m_fileWav(NULL)
	, m_sAudioFmt()
	, m_rect()
	, m_server()
	, m_fRatio(1.0f)
{
	ZeroMemory(&m_sAudioFmt, sizeof(SFgAudioFrame));
	ZeroMemory(&m_rect, sizeof(SDL_Rect));
	m_mutexAudio = CreateMutex(NULL, FALSE, NULL);
	m_mutexVideo = CreateMutex(NULL, FALSE, NULL);
}

CSDLPlayer::~CSDLPlayer()
{
	unInit();

	CloseHandle(m_mutexAudio);
	CloseHandle(m_mutexVideo);
}

bool CSDLPlayer::init()
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return false;
	}

	/* Clean up on exit, exit on window close and interrupt */
	atexit(SDL_Quit);

	SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
	SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_IGNORE);

	initVideo(600, 400);

	/* Filter quit and mouse motion events */
	SDL_SetEventFilter(FilterEvents);

	m_server.start(this);

	return true;
}

void CSDLPlayer::unInit()
{
	unInitVideo();
	unInitAudio();

	SDL_Quit();
}

void CSDLPlayer::loopEvents()
{
	SDL_Event event;

	BOOL bEndLoop = FALSE;
	/* Loop waiting for ESC+Mouse_Button */
	while (SDL_WaitEvent(&event) >= 0) {		
		switch (event.type) {
		case SDL_VIDEORESIZE: {
			unsigned int wSource = m_rect.w;
			unsigned int wDst = event.resize.w;
			unsigned int hSource = m_rect.h;
			unsigned int hDst = event.resize.h;

			if (wDst != wSource || hDst != hSource)
			{
				unInitVideo();
				initVideo(wDst, hDst);
			}
			break;
		}
		case SDL_VIDEOEXPOSE: {
			break;
		}
		case SDL_ACTIVEEVENT: {
			if (event.active.state & SDL_APPACTIVE) {
				if (event.active.gain) {
					//printf("App activated\n");
				}
				else {
					//printf("App iconified\n");
				}
			}
			break;
		}
		case SDL_KEYUP: {
			switch (event.key.keysym.sym)
			{
				case SDLK_q: {
					printf("key down");
					m_server.stop();
					SDL_WM_SetCaption("AirPlay Demo - Stopped [s - start server, q - stop server]", NULL);
					break;
				}
				case SDLK_s: {
					printf("key down");
					m_server.start(this);
					SDL_WM_SetCaption("AirPlay Demo - Started [s - start server, q - stop server]", NULL);
					break;
				}
				case SDLK_EQUALS: {
					m_fRatio *= 2;
					m_fRatio = m_server.setVideoScale(m_fRatio);
					break;
				}
				case SDLK_MINUS: {
					m_fRatio /= 2;
					m_fRatio = m_server.setVideoScale(m_fRatio);
					break;
				}
			}
				break;
		}

		case SDL_QUIT: {
			printf("Quit requested, quitting.\n");
			m_server.stop();
			bEndLoop = TRUE;
			break;
		}
		}
		if (bEndLoop)
		{
			break;
		}
	}
}

void CSDLPlayer::outputVideo(SFgVideoFrame* data) {
	if (data->width == 0 || data->height == 0) {
		return;
	}

	CAutoLock oLock(m_mutexVideo, "outputVideo");
	if (m_yuv == NULL) {
		return;
	}

	SDL_LockYUVOverlay(m_yuv);

	// Calculate scaling factors
	double scaleX = static_cast<double>(m_rect.w) / data->width;
	double scaleY = static_cast<double>(m_rect.h) / data->height;

	// Rescale YUV planes
	for (int y = 0; y < m_rect.h; ++y) {
		int srcY = static_cast<int>(y / scaleY);
		if (srcY >= data->height) break;

		for (int x = 0; x < m_rect.w; ++x) {
			int srcX = static_cast<int>(x / scaleX);
			if (srcX >= data->width) break;

			m_yuv->pixels[0][y * m_yuv->pitches[0] + x] = data->data[srcY * data->pitch[0] + srcX];
		}
	}

	for (int y = 0; y < m_rect.h / 2; ++y) {
		int srcY = static_cast<int>(y / scaleY);
		if (srcY >= data->height / 2) break;

		for (int x = 0; x < m_rect.w / 2; ++x) {
			int srcX = static_cast<int>(x / scaleX);
			if (srcX >= data->width / 2) break;

			m_yuv->pixels[1][y * m_yuv->pitches[1] + x] = data->data[data->dataLen[0] + srcY * data->pitch[1] + srcX];
			m_yuv->pixels[2][y * m_yuv->pitches[2] + x] = data->data[data->dataLen[0] + data->dataLen[1] + srcY * data->pitch[2] + srcX];
		}
	}

	SDL_UnlockYUVOverlay(m_yuv);

	m_rect.x = 0;
	m_rect.y = 0;

	SDL_DisplayYUVOverlay(m_yuv, &m_rect);
}

void CSDLPlayer::outputAudio(SFgAudioFrame* data)
{
	if (data->channels == 0) {
		return;
	}

	initAudio(data);

	if (m_bDumpAudio) {
		if (m_fileWav != NULL) {
			fwrite(data->data, data->dataLen, 1, m_fileWav);
		}
	}

	SDemoAudioFrame* dataClone = new SDemoAudioFrame();
	dataClone->dataTotal = data->dataLen;
	dataClone->pts = data->pts;
	dataClone->dataLeft = dataClone->dataTotal;
	dataClone->data = new uint8_t[dataClone->dataTotal];
	memcpy(dataClone->data, data->data, dataClone->dataTotal);

	{
		CAutoLock oLock(m_mutexAudio, "outputAudio");
		m_queueAudio.push(dataClone);
	}
}

void CSDLPlayer::initVideo(int width, int height) {
	// Set video mode with specified width and height
	m_surface = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE | SDL_RESIZABLE);
	SDL_WM_SetCaption("AirPlay Demo [s - start server, q - stop server]", NULL);

	{
		CAutoLock oLock(m_mutexVideo, "initVideo");

		// Create a YUV overlay with the specified width and height
		m_yuv = SDL_CreateYUVOverlay(width, height, SDL_IYUV_OVERLAY, m_surface);

		if (m_yuv) {
			// Initialize Y plane (Y component of the YUV image)
			memset(m_yuv->pixels[0], 0, m_yuv->pitches[0] * m_yuv->h);
			// Initialize U plane (U component of the YUV image) with 128 (neutral color)
			memset(m_yuv->pixels[1], 128, m_yuv->pitches[1] * (m_yuv->h >> 1));
			// Initialize V plane (V component of the YUV image) with 128 (neutral color)
			memset(m_yuv->pixels[2], 128, m_yuv->pitches[2] * (m_yuv->h >> 1));
		}

		// Set the destination rectangle size to match the input dimensions
		m_rect.x = 0;
		m_rect.y = 0;
		m_rect.w = width;
		m_rect.h = height;

		// Display the initialized YUV overlay
		SDL_DisplayYUVOverlay(m_yuv, &m_rect);
	}
}

void CSDLPlayer::unInitVideo()
{
	if (NULL != m_surface) {
		SDL_FreeSurface(m_surface);
		m_surface = NULL;
	}

	{
		CAutoLock oLock(m_mutexVideo, "unInitVideo");
		if (NULL != m_yuv) {
			SDL_FreeYUVOverlay(m_yuv);
			m_yuv = NULL;
		}
		m_rect.w = 0;
		m_rect.h = 0;
	}

	unInitAudio();
}

void CSDLPlayer::initAudio(SFgAudioFrame* data)
{
	if ((data->sampleRate != m_sAudioFmt.sampleRate || data->channels != m_sAudioFmt.channels)) {
		unInitAudio();
	}
	if (!m_bAudioInited) {
		SDL_AudioSpec wanted_spec, obtained_spec;
		wanted_spec.freq = data->sampleRate;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = data->channels;
		wanted_spec.silence = 0;
		wanted_spec.samples = 1920;
		wanted_spec.callback = sdlAudioCallback;
		wanted_spec.userdata = this;

		if (SDL_OpenAudio(&wanted_spec, &obtained_spec) < 0)
		{
			printf("can't open audio.\n");
			return;
		}

		SDL_PauseAudio(1);

		m_sAudioFmt.bitsPerSample = data->bitsPerSample;
		m_sAudioFmt.channels = data->channels;
		m_sAudioFmt.sampleRate = data->sampleRate;
		m_bAudioInited = true;

		if (m_bDumpAudio) {
			m_fileWav = fopen("demo-audio.wav", "wb");
		}
	}
	if (m_queueAudio.size() > 5) {
		SDL_PauseAudio(0);
	}
}

void CSDLPlayer::unInitAudio()
{
	SDL_CloseAudio();
	m_bAudioInited = false;
	memset(&m_sAudioFmt, 0, sizeof(m_sAudioFmt));

	{
		CAutoLock oLock(m_mutexAudio, "unInitAudio");
		while (!m_queueAudio.empty())
		{
			SDemoAudioFrame* pAudioFrame = m_queueAudio.front();
			delete[] pAudioFrame->data;
			delete pAudioFrame;
			m_queueAudio.pop();
		}
	}

	if (m_fileWav != NULL) {
		fclose(m_fileWav);
		m_fileWav = NULL;
	}
}

void CSDLPlayer::sdlAudioCallback(void* userdata, Uint8* stream, int len)
{
	CSDLPlayer* pThis = (CSDLPlayer*)userdata;
	int needLen = len;
	int streamPos = 0;
	memset(stream, 0, len);

	CAutoLock oLock(pThis->m_mutexAudio, "sdlAudioCallback");
	while (!pThis->m_queueAudio.empty())
	{
		SDemoAudioFrame* pAudioFrame = pThis->m_queueAudio.front();
		int pos = pAudioFrame->dataTotal - pAudioFrame->dataLeft;
		int readLen = min(pAudioFrame->dataLeft, needLen);

		//SDL_MixAudio(stream + streamPos, pAudioFrame->data + pos, readLen, 100);
		memcpy(stream + streamPos, pAudioFrame->data + pos, readLen);

		pAudioFrame->dataLeft -= readLen;
		needLen -= readLen;
		streamPos += readLen;
		if (pAudioFrame->dataLeft <= 0) {
			pThis->m_queueAudio.pop();
			delete[] pAudioFrame->data;
			delete pAudioFrame;
		}
		if (needLen <= 0) {
			break;
		}
	}
}
