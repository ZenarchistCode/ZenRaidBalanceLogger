class CfgPatches
{
	class ZenRaidBalanceLogger
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = { "DZ_Data", "DZ_Scripts", "DayZExpansion_BaseBuilding_Scripts", "HDSN_BreachingCharge" };
	};
};

class CfgMods
{
	class ZenRaidBalanceLogger
	{
		dir = "ZenRaidBalanceLogger";
		picture = "";
		action = "";
		hideName = 1;
		hidePicture = 1;
		name = "Zen Raid Balance Logger";
		credits = "";
		author = "Zen";
		authorID = "0";
		version = "1.0";
		extra = 0;
		type = "mod";
		dependencies[] = {"Game", "World", "Mission"};
		class defs
		{
			class worldScriptModule
			{
				value = "";
				files[] = {"ZenRaidBalanceLogger/scripts/4_World"};
			};
			class missionScriptModule
			{
				value = "";
				files[] = {"ZenRaidBalanceLogger/scripts/5_Mission"};
			};
		};
	};
};
