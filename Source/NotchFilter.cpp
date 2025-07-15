/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2025 Open Ephys

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "NotchFilter.h"

#include "NotchFilterEditor.h"

#include <stdio.h>


void NotchFilterSettings::createFilters(int numChannels, float sampleRate_, double lowCut, double highCut)
{

    sampleRate = sampleRate_;

    filters.clear();

    for (int n = 0; n < numChannels; ++n)
    {
        filters.add(new Dsp::SmoothedFilterDesign
            < Dsp::Butterworth::Design::BandStop  // design type
            <4>,                                   // order
            1,                                     // number of channels (must be const)
            Dsp::DirectFormII>(1));               // realization

    }

    updateFilters(lowCut, highCut);
}

void NotchFilterSettings::updateFilters(double lowCut, double highCut)
{
    for (int n = 0; n < filters.size(); n++)
    {
        setFilterParameters(lowCut, highCut, n);
    }
}

void NotchFilterSettings::setFilterParameters(double lowCut, double highCut, int channel)
{
    Dsp::Params params;
    params[0] = sampleRate;                 // sample rate
    params[1] = 4;                          // order
    params[2] = (highCut + lowCut) / 2;     // center frequency
    params[3] = highCut - lowCut;           // bandwidth

    filters[channel]->setParams(params);
}

FilterJob::FilterJob (String name, Array<Dsp::Filter*> filters_, Array<float*> channelPointers_, int numSamples_)
    : ThreadPoolJob (name),
      filters (filters_),
      channelPointers (channelPointers_),
      numSamples (numSamples_),
      numChannels (channelPointers_.size())
{
}

ThreadPoolJob::JobStatus FilterJob::runJob()
{
    for (int i = 0; i < numChannels; i++)
    {
        float* ptr = channelPointers[i];
        filters[i]->process (numSamples, &ptr);
    }

    return ThreadPoolJob::jobHasFinished;
}


NotchFilter::NotchFilter()
    : GenericProcessor("Notch Filter")
{
    threadPool = std::make_unique<ThreadPool>(4);
}

void NotchFilter::registerParameters()
{

    addFloatParameter (Parameter::STREAM_SCOPE, "low_cut", "Low cut", "Filter low cut", "Hz", 59, 1, 100, 1.0, false);
    addFloatParameter (Parameter::STREAM_SCOPE, "high_cut", "High cut", "Filter high cut", "Hz", 61, 1, 100, 1.0, false);
    addMaskChannelsParameter (Parameter::STREAM_SCOPE, "channels", "Channels", "Channels to filter for this stream");

    Array<String> numThreads { "1", "4", "8", "16", "32", "64" };
    addCategoricalParameter (Parameter::PROCESSOR_SCOPE, "threads", "Threads", "Number of threads to use", numThreads, 1, true);

}

AudioProcessorEditor* NotchFilter::createEditor()
{
    editor = std::make_unique<NotchFilterEditor>(this);

    return editor.get();
}

void NotchFilter::updateSettings()
{
    settings.update(getDataStreams());

    for (auto stream : getDataStreams())
    {
        settings[stream->getStreamId()]->createFilters(
            stream->getChannelCount(),
            stream->getSampleRate(),
            (*stream)["low_cut"],
            (*stream)["high_cut"]
        );
    }

    int totalChans = getTotalContinuousChannels();
    int numThreads = threadPool->getNumThreads();

    if (totalChans <= 32)
    {
        getParameter("threads")->setNextValue (0, false); // set to 1 thread
    }
    else if (totalChans > 32 && numThreads < 4)
    {
        getParameter("threads")->setNextValue (1, false); // set to 4 threads
    }
}


void NotchFilter::parameterValueChanged(Parameter* param)
{

    uint16 currentStream = param->getStreamId();

    if (param->getName().equalsIgnoreCase ("low_cut"))
    {
        if ((*getDataStream (currentStream))["low_cut"] >= (*getDataStream (currentStream))["high_cut"])
        {
            getDataStream (currentStream)->getParameter ("low_cut")->restorePreviousValue();
            return;
        }

        settings[currentStream]->updateFilters (
            (*getDataStream (currentStream))["low_cut"],
            (*getDataStream (currentStream))["high_cut"]);
    }
    else if (param->getName().equalsIgnoreCase ("high_cut"))
    {
        if ((*getDataStream (currentStream))["high_cut"] <= (*getDataStream (currentStream))["low_cut"])
        {
            getDataStream (currentStream)->getParameter ("high_cut")->restorePreviousValue();
            return;
        }

        settings[currentStream]->updateFilters (
            (*getDataStream (currentStream))["low_cut"],
            (*getDataStream (currentStream))["high_cut"]);
    }
    else if (param->getName().equalsIgnoreCase ("threads"))
    {
        if (threadPool->getNumJobs() > 0)
        {
            param->restorePreviousValue();
            return;
        }

        int numThreads = param->getValueAsString().getIntValue();
        threadPool.reset (new ThreadPool (numThreads));
    }
}

bool NotchFilter::stopAcquisition()
{
    return threadPool->removeAllJobs (true, 1000);
}

void NotchFilter::process(AudioBuffer<float>& buffer)
{

    for (auto stream : getDataStreams())
    {
        if ((*stream)["enable_stream"])
        {
            NotchFilterSettings* streamSettings = settings[stream->getStreamId()];

            const uint16 streamId = stream->getStreamId();
            const uint32 numSamples = getNumSamplesInBlock (streamId);

            int i = 0;
            Array<float*> channelPointers;
            Array<Dsp::Filter*> filters;

            for (auto localChannelIndex : *((*stream)["channels"].getArray()))
            {
                int globalChannelIndex = getGlobalChannelIndex (stream->getStreamId(), (int) localChannelIndex);

                channelPointers.add (buffer.getWritePointer (globalChannelIndex));
                filters.add (streamSettings->filters[localChannelIndex]);
                i++;

                if (i % CHANNELS_PER_THREAD == 0)
                {
                    String jobName = String (streamId) + "_" + String (i++);

                    FilterJob* job = new FilterJob (jobName, filters, channelPointers, numSamples);

                    threadPool->addJob (job, true);

                    channelPointers.clear();
                    filters.clear();
                }
            }

            if (channelPointers.size() > 0)
            {
                String jobName = String (streamId) + "_" + String (i++);

                FilterJob* job = new FilterJob (jobName, filters, channelPointers, numSamples);

                threadPool->addJob (job, true);
            }
        }
    }

    while (threadPool->getNumJobs() > 0)
        std::this_thread::sleep_for (std::chrono::microseconds (50));
}