#include "../include/Clip.h"

using namespace openshot;

// Init default settings for a clip
void Clip::init_settings()
{
	// Init clip settings
	Position(0.0);
	Layer(0);
	Start(0.0);
	End(0.0);
	gravity = GRAVITY_CENTER;
	scale = SCALE_FIT;
	anchor = ANCHOR_CANVAS;

	// Init scale curves
	scale_x = Keyframe(100.0);
	scale_y = Keyframe(100.0);

	// Init location curves
	location_x = Keyframe(0.0);
	location_y = Keyframe(0.0);

	// Init alpha & rotation
	alpha = Keyframe(100.0);
	rotation = Keyframe(0.0);

	// Init time & volume
	time = Keyframe(0.0);
	volume = Keyframe(100.0);

	// Init crop settings
	crop_gravity = GRAVITY_CENTER;
	crop_width = Keyframe(-1.0);
	crop_height = Keyframe(-1.0);
	crop_x = Keyframe(0.0);
	crop_y = Keyframe(0.0);

	// Init shear and perspective curves
	shear_x = Keyframe(0.0);
	shear_y = Keyframe(0.0);
	perspective_c1_x = Keyframe(-1.0);
	perspective_c1_y = Keyframe(-1.0);
	perspective_c2_x = Keyframe(-1.0);
	perspective_c2_y = Keyframe(-1.0);
	perspective_c3_x = Keyframe(-1.0);
	perspective_c3_y = Keyframe(-1.0);
	perspective_c4_x = Keyframe(-1.0);
	perspective_c4_y = Keyframe(-1.0);

	// Default pointers
	file_reader = NULL;
	resampler = NULL;

	cout << "INIT CLIP SETTINGS!!!!" << endl;
}

// Default Constructor for a clip
Clip::Clip()
{
	// Init all default settings
	init_settings();
}

// Constructor with reader
Clip::Clip(FileReaderBase* reader)
{
	// Init all default settings
	init_settings();

	// set reader pointer
	file_reader = reader;
}

// Constructor with filepath
Clip::Clip(string path)
{
	// Init all default settings
	init_settings();

	// Get file extension (and convert to lower case)
	string ext = get_file_extension(path);
	transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	// Determine if common video formats
	if (ext=="avi" || ext=="mov" || ext=="mkv" ||  ext=="mpg" || ext=="mpeg" || ext=="mp3" || ext=="mp4" || ext=="mts" ||
		ext=="ogg" || ext=="wav" || ext=="wmv" || ext=="webm" || ext=="vob")
	{
		try
		{
			// Open common video format
			file_reader = new FFmpegReader(path);
			cout << "READER FOUND: FFmpegReader" << endl;
		} catch(...) { }
	}

	// If no video found, try each reader
	if (!file_reader)
	{
		try
		{
			// Try an image reader
			file_reader = new ImageReader(path);
			cout << "READER FOUND: ImageReader" << endl;

		} catch(...) {
			try
			{
				// Try a video reader
				file_reader = new FFmpegReader(path);
				cout << "READER FOUND: FFmpegReader" << endl;

			} catch(BaseException ex) {
				// No Reader Found, Throw an exception
				cout << "READER NOT FOUND" << endl;
				throw ex;
			}
		}
	}

}

/// Set the current reader
void Clip::Reader(FileReaderBase* reader)
{
	// set reader pointer
	file_reader = reader;
}

/// Get the current reader
FileReaderBase* Clip::Reader()
{
	return file_reader;
}

// Open the internal reader
void Clip::Open() throw(InvalidFile)
{
	if (file_reader)
	{
		// Open the reader
		file_reader->Open();

		// Set some clip properties from the file reader
		End(file_reader->info.duration);
	}
}

// Close the internal reader
void Clip::Close()
{
	if (file_reader)
		file_reader->Close();
}

// Get end position of clip (trim end of video), which can be affected by the time curve.
float Clip::End()
{
	// Determine the FPS fo this clip
	float fps = 24.0;
	if (file_reader)
		// file reader
		fps = file_reader->info.fps.ToFloat();

	// if a time curve is present, use it's length
	if (time.Points.size() > 1)
		return float(time.GetLength()) / fps;
	else
		// just use the duration (as detected by the reader)
		return end;
}

// Get an openshot::Frame object for a specific frame number of this reader.
tr1::shared_ptr<Frame> Clip::GetFrame(int requested_frame) throw(ReaderClosed)
{
	// Adjust out of bounds frame number
	requested_frame = adjust_frame_number_minimum(requested_frame);

	// Is a time map detected
	int new_frame_number = requested_frame;
	if (time.Values.size() > 1)
		new_frame_number = time.GetInt(requested_frame);

	// Now that we have re-mapped what frame number is needed, go and get the frame pointer
	tr1::shared_ptr<Frame> frame = file_reader->GetFrame(new_frame_number);

	// Get time mapped frame number (used to increase speed, change direction, etc...)
	tr1::shared_ptr<Frame> new_frame = get_time_mapped_frame(frame, requested_frame);

	// Apply basic image processing (scale, rotation, etc...)
	//apply_basic_image_processing(new_frame, new_frame_number);

	// Return processed 'frame'
	return new_frame;
}

// Get file extension
string Clip::get_file_extension(string path)
{
	// return last part of path
	return path.substr(path.find_last_of(".") + 1);
}

// Reverse an audio buffer
void Clip::reverse_buffer(juce::AudioSampleBuffer* buffer)
{
	int number_of_samples = buffer->getNumSamples();
	int channels = buffer->getNumChannels();

	// Reverse array (create new buffer to hold the reversed version)
	AudioSampleBuffer *reversed = new juce::AudioSampleBuffer(channels, number_of_samples);
	reversed->clear();

	for (int channel = 0; channel < channels; channel++)
	{
		int n=0;
		for (int s = number_of_samples - 1; s >= 0; s--, n++)
			reversed->getSampleData(channel)[n] = buffer->getSampleData(channel)[s];
	}

	// Copy the samples back to the original array
	buffer->clear();
	// Loop through channels, and get audio samples
	for (int channel = 0; channel < channels; channel++)
		// Get the audio samples for this channel
		buffer->addFrom(channel, 0, reversed->getSampleData(channel), number_of_samples, 1.0f);

	delete reversed;
	reversed = NULL;
}

// Adjust the audio and image of a time mapped frame
tr1::shared_ptr<Frame> Clip::get_time_mapped_frame(tr1::shared_ptr<Frame> frame, int frame_number)
{
	cout << "TIME MAPPER: " << frame_number << endl;
	tr1::shared_ptr<Frame> new_frame;

	// Check for a valid time map curve
	if (time.Values.size() > 1)
	{
		// create buffer and resampler
		juce::AudioSampleBuffer *samples = NULL;
		if (!resampler)
			resampler = new AudioResampler();

		// Get new frame number
		int new_frame_number = time.GetInt(frame_number);

		// Create a new frame
		int samples_in_frame = GetSamplesPerFrame(new_frame_number, file_reader->info.fps);
		new_frame = tr1::shared_ptr<Frame>(new Frame(new_frame_number, 1, 1, "#000000", samples_in_frame, frame->GetAudioChannelsCount()));

		// Copy the image from the new frame
		new_frame->AddImage(file_reader->GetFrame(new_frame_number)->GetImage());


		// Get delta (difference in previous Y value)
		int delta = int(round(time.GetDelta(frame_number)));

		// Init audio vars
		int sample_rate = file_reader->GetFrame(new_frame_number)->GetAudioSamplesRate();
		int channels = file_reader->info.channels;
		int number_of_samples = file_reader->GetFrame(new_frame_number)->GetAudioSamplesCount();

		// Determine if we are speeding up or slowing down
		if (time.GetRepeatFraction(frame_number).den > 1)
		{
			// Resample data, and return new buffer pointer
			AudioSampleBuffer *buffer = NULL;
			int resampled_buffer_size = 0;

			if (time.GetRepeatFraction(frame_number).num == 1)
			{
				// SLOW DOWN audio (split audio)
				samples = new juce::AudioSampleBuffer(channels, number_of_samples);
				samples->clear();

				// Loop through channels, and get audio samples
				for (int channel = 0; channel < channels; channel++)
					// Get the audio samples for this channel
					samples->addFrom(channel, 0, file_reader->GetFrame(new_frame_number)->GetAudioSamples(channel), number_of_samples, 1.0f);

				// Reverse the samples (if needed)
				if (!time.IsIncreasing(frame_number))
					reverse_buffer(samples);

				// Resample audio to be X times slower (where X is the denominator of the repeat fraction)
				resampler->SetBuffer(samples, 1.0 / time.GetRepeatFraction(frame_number).den);

				// Resample the data (since it's the 1st slice)
				buffer = resampler->GetResampledBuffer();

				// Save the resampled data in the cache
				audio_cache = new juce::AudioSampleBuffer(channels, buffer->getNumSamples());
				audio_cache->clear();
				for (int channel = 0; channel < channels; channel++)
					// Get the audio samples for this channel
					audio_cache->addFrom(channel, 0, buffer->getSampleData(channel), buffer->getNumSamples(), 1.0f);
			}

			// Get the length of the resampled buffer
			resampled_buffer_size = audio_cache->getNumSamples();

			// Just take the samples we need for the requested frame
			int start = (number_of_samples * (time.GetRepeatFraction(frame_number).num - 1));
			if (start > 0)
				start -= 1;
			for (int channel = 0; channel < channels; channel++)
				// Add new (slower) samples, to the frame object
				new_frame->AddAudio(channel, 0, audio_cache->getSampleData(channel, start), number_of_samples, 1.0f);

			// Clean up if the final section
			if (time.GetRepeatFraction(frame_number).num == time.GetRepeatFraction(frame_number).den)
			{
				// Clear, since we don't want it maintain state yet
				delete audio_cache;
				audio_cache = NULL;
			}

			// Clean up
			buffer = NULL;


			// Determine next unique frame (after these repeating frames)
			int next_unique_frame = time.GetInt(frame_number + (time.GetRepeatFraction(frame_number).den - time.GetRepeatFraction(frame_number).num) + 1);
			if (next_unique_frame != new_frame_number)
				// Overlay the next frame on top of this frame (to create a smoother slow motion effect)
				new_frame->AddImage(file_reader->GetFrame(next_unique_frame)->GetImage(), float(time.GetRepeatFraction(frame_number).num) / float(time.GetRepeatFraction(frame_number).den));

		}
		else if (abs(delta) > 1 && abs(delta) < 100)
		{
			// SPEED UP (multiple frames of audio), as long as it's not more than X frames
			samples = new juce::AudioSampleBuffer(channels, number_of_samples * abs(delta));
			samples->clear();
			int start = 0;

			if (delta > 0)
			{
				// Loop through each frame in this delta
				for (int delta_frame = new_frame_number - (delta - 1); delta_frame <= new_frame_number; delta_frame++)
				{
					// buffer to hold detal samples
					int number_of_delta_samples = file_reader->GetFrame(delta_frame)->GetAudioSamplesCount();
					AudioSampleBuffer* delta_samples = new juce::AudioSampleBuffer(channels, number_of_delta_samples);
					delta_samples->clear();

					for (int channel = 0; channel < channels; channel++)
						delta_samples->addFrom(channel, 0, file_reader->GetFrame(delta_frame)->GetAudioSamples(channel), number_of_delta_samples, 1.0f);

					// Reverse the samples (if needed)
					if (!time.IsIncreasing(frame_number))
						reverse_buffer(delta_samples);

					// Copy the samples to
					for (int channel = 0; channel < channels; channel++)
						// Get the audio samples for this channel
						samples->addFrom(channel, start, delta_samples->getSampleData(channel), number_of_delta_samples, 1.0f);

					// Clean up
					delete delta_samples;
					delta_samples = NULL;

					// Increment start position
					start += number_of_delta_samples;
				}
			}
			else
			{
				// Loop through each frame in this delta
				for (int delta_frame = new_frame_number - (delta + 1); delta_frame >= new_frame_number; delta_frame--)
				{
					// buffer to hold delta samples
					int number_of_delta_samples = file_reader->GetFrame(delta_frame)->GetAudioSamplesCount();
					AudioSampleBuffer* delta_samples = new juce::AudioSampleBuffer(channels, number_of_delta_samples);
					delta_samples->clear();

					for (int channel = 0; channel < channels; channel++)
						delta_samples->addFrom(channel, 0, file_reader->GetFrame(delta_frame)->GetAudioSamples(channel), number_of_delta_samples, 1.0f);

					// Reverse the samples (if needed)
					if (!time.IsIncreasing(frame_number))
						reverse_buffer(delta_samples);

					// Copy the samples to
					for (int channel = 0; channel < channels; channel++)
						// Get the audio samples for this channel
						samples->addFrom(channel, start, delta_samples->getSampleData(channel), number_of_delta_samples, 1.0f);

					// Clean up
					delete delta_samples;
					delta_samples = NULL;

					// Increment start position
					start += number_of_delta_samples;
				}
			}

			// Resample audio to be X times faster (where X is the delta of the repeat fraction)
			resampler->SetBuffer(samples, float(start) / float(number_of_samples));

			// Resample data, and return new buffer pointer
			AudioSampleBuffer *buffer = resampler->GetResampledBuffer();
			int resampled_buffer_size = buffer->getNumSamples();

			// Add the newly resized audio samples to the current frame
			for (int channel = 0; channel < channels; channel++)
				// Add new (slower) samples, to the frame object
				new_frame->AddAudio(channel, 0, buffer->getSampleData(channel), number_of_samples, 1.0f);

			// Clean up
			buffer = NULL;
		}
		else
		{
			// Use the samples on this frame (but maybe reverse them if needed)
			samples = new juce::AudioSampleBuffer(channels, number_of_samples);
			samples->clear();

			// Loop through channels, and get audio samples
			for (int channel = 0; channel < channels; channel++)
				// Get the audio samples for this channel
				samples->addFrom(channel, 0, frame->GetAudioSamples(channel), number_of_samples, 1.0f);

			// reverse the samples
			reverse_buffer(samples);

			// Add reversed samples to the frame object
			for (int channel = 0; channel < channels; channel++)
				new_frame->AddAudio(channel, 0, samples->getSampleData(channel), number_of_samples, 1.0f);


		}

		// clean up
		//delete resampler;
		//resampler = NULL;
		cout << "samples: "<< samples << endl;
		delete samples;
		samples = NULL;
	} else
		// Use original frame
		new_frame = frame;

	// Return new time mapped frame
	return new_frame;

}

// Apply basic image processing (scale, rotate, move, etc...)
void Clip::apply_basic_image_processing(tr1::shared_ptr<Frame> frame, int frame_number)
{
	// Get values
	float rotation_value = rotation.GetValue(frame_number);
	//float scale_x_value = scale_x.GetValue(frame_number);
	//float scale_y_value = scale_y.GetValue(frame_number);

	// rotate frame
	if (rotation_value != 0)
		frame->Rotate(rotation_value);
}

// Adjust frame number minimum value
int Clip::adjust_frame_number_minimum(int frame_number)
{
	// Never return a frame number 0 or below
	if (frame_number < 1)
		return 1;
	else
		return frame_number;

}

// Calculate the # of samples per video frame (for a specific frame number)
int Clip::GetSamplesPerFrame(int frame_number, Fraction rate)
{
	// Get the total # of samples for the previous frame, and the current frame (rounded)
	double fps = rate.Reciprocal().ToDouble();
	double previous_samples = round((file_reader->info.sample_rate * fps) * (frame_number - 1));
	double total_samples = round((file_reader->info.sample_rate * fps) * frame_number);

	// Subtract the previous frame's total samples with this frame's total samples.  Not all sample rates can
	// be evenly divided into frames, so each frame can have have different # of samples.
	double samples_per_frame = total_samples - previous_samples;
	return samples_per_frame;
}