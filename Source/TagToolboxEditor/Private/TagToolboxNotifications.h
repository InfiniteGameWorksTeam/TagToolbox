// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

/**
 * The one toast helper: refusals and outcomes surface as notifications, never
 * silence, and every surface uses the same shape.
 */
namespace TagToolboxNotifications
{
	inline void Show(const FText& Message, float ExpireDuration = 5.0f)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = ExpireDuration;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}
