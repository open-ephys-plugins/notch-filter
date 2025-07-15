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

#include "NotchFilterEditor.h"


NotchFilterEditor::NotchFilterEditor(GenericProcessor* parentNode) : GenericEditor(parentNode)
{
    desiredWidth = 165;

    addBoundedValueParameterEditor (Parameter::STREAM_SCOPE, "low_cut", 15, 29);
    addBoundedValueParameterEditor (Parameter::STREAM_SCOPE, "high_cut", 15, 54);
    addMaskChannelsParameterEditor (Parameter::STREAM_SCOPE, "channels", 15, 79);
    addComboBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "threads", 15, 104);

}