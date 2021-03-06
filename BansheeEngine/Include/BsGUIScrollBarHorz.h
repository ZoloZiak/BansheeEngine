//__________________________ Banshee Project - A modern game development toolkit _________________________________//
//_____________________________________ www.banshee-project.com __________________________________________________//
//________________________ Copyright (c) 2014 Marko Pintera. All rights reserved. ________________________________//
#pragma once

#include "BsPrerequisites.h"
#include "BsGUIScrollBar.h"
#include "BsEvent.h"

namespace BansheeEngine
{
	/**
	 * @brief	Specialization of a horizontal scroll bar.
	 */
	class BS_EXPORT GUIScrollBarHorz : public GUIScrollBar
	{
	public:
		/**
		 * Returns type name of the GUI element used for finding GUI element styles. 
		 */
		static const String& getGUITypeName();

		/**
		 * @brief	Creates a new horizontal scroll bar.
		 *
		 * @param	styleName		Optional style to use for the element. Style will be retrieved
		 *							from GUISkin of the GUIWidget the element is used on. If not specified
		 *							default style is used.
		 */
		static GUIScrollBarHorz* create(const String& styleName = StringUtil::BLANK);

		/**
		 * @brief	Creates a new horizontal scroll bar.
		 *
		 * @param	layoutOptions	Options that allows you to control how is the element positioned in
		 *							GUI layout. This will override any similar options set by style.
		 * @param	styleName		Optional style to use for the element. Style will be retrieved
		 *							from GUISkin of the GUIWidget the element is used on. If not specified
		 *							default style is used.
		 */
		static GUIScrollBarHorz* create(const GUIOptions& layoutOptions, const String& styleName = StringUtil::BLANK);
	protected:
		GUIScrollBarHorz(const String& styleName, const GUILayoutOptions& layoutOptions);
		~GUIScrollBarHorz();
	};
}