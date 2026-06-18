modded class MissionServer
{
	override void OnInit()
	{
		super.OnInit();
		ZenRaidBalanceLogger.ScheduleReport();
	}
}
