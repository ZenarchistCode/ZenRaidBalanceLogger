class ZenRaidBalanceBreakdown
{
	string Type;
	int Count;
	int Damage;

	void ZenRaidBalanceBreakdown(string type = "")
	{
		Type = type;
		Count = 0;
		Damage = 0;
	}

	void Add(int damage)
	{
		Count++;
		Damage += damage;
	}
}

class ZenRaidBalanceTerritoryStats
{
	int TerritoryID;
	string TerritoryName;
	vector TerritoryPosition;
	int RaidDamageRequired;
	int RaidDamageOutputStored;
	int RaidTargetCount;
	int StoredChargeCount;
	ref map<string, ref ZenRaidBalanceBreakdown> RequiredByType;
	ref map<string, ref ZenRaidBalanceBreakdown> OutputByType;

	void ZenRaidBalanceTerritoryStats(int id = -1, string name = "UNKNOWN", vector position = "0 0 0")
	{
		TerritoryID = id;
		TerritoryName = name;
		TerritoryPosition = position;
		RaidDamageRequired = 0;
		RaidDamageOutputStored = 0;
		RaidTargetCount = 0;
		StoredChargeCount = 0;
		RequiredByType = new map<string, ref ZenRaidBalanceBreakdown>;
		OutputByType = new map<string, ref ZenRaidBalanceBreakdown>;
	}

	void AddRequired(string type, int damage)
	{
		if (damage <= 0)
			return;

		RaidTargetCount++;
		RaidDamageRequired += damage;
		AddBreakdown(RequiredByType, type, damage);
	}

	void AddOutput(string type, int damage)
	{
		if (damage <= 0)
			return;

		StoredChargeCount++;
		RaidDamageOutputStored += damage;
		AddBreakdown(OutputByType, type, damage);
	}

	void AddBreakdown(map<string, ref ZenRaidBalanceBreakdown> breakdownMap, string type, int damage)
	{
		ZenRaidBalanceBreakdown breakdown;
		if (!breakdownMap.Find(type, breakdown))
		{
			breakdown = new ZenRaidBalanceBreakdown(type);
			breakdownMap.Insert(type, breakdown);
		}

		breakdown.Add(damage);
	}
}

class ZenRaidBalanceRegistry
{
	static ref set<ItemBase> TRACKED_ITEMS = new set<ItemBase>;

	static void Register(ItemBase item)
	{
		if (!g_Game || !g_Game.IsDedicatedServer())
			return;

		if (!item)
			return;

		if (!IsTrackedItem(item))
			return;

		TRACKED_ITEMS.Insert(item);
	}

	static void Unregister(ItemBase item)
	{
		if (!TRACKED_ITEMS || !item)
			return;

		int index = TRACKED_ITEMS.Find(item);
		if (index >= 0)
			TRACKED_ITEMS.Remove(index);
	}

	static bool IsTrackedItem(ItemBase item)
	{
		if (HDSN_BreachingChargeBase.Cast(item))
			return true;

		if (BaseBuildingBase.Cast(item))
			return true;

		return false;
	}
}

class ZenRaidBalanceLogger
{
	static bool REPORT_SCHEDULED = false;
	static bool REPORT_PRINTED = false;
	static int REPORT_DELAY_MS = 60000;
	static bool PRINT_EMPTY_TERRITORIES = false;
	static int MAX_BREAKDOWN_LINES = 12;
	static string LOG_ROOT = "$profile:/Zenarchist";
	static string LOG_DIR = "$profile:/Zenarchist/Logs";
	static FileHandle LOG_FILE;

	static void ScheduleReport()
	{
		if (!g_Game || !g_Game.IsDedicatedServer())
			return;

		if (REPORT_SCHEDULED)
			return;

		REPORT_SCHEDULED = true;
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(PrintReport, REPORT_DELAY_MS, false);
	}

	static void PrintReport()
	{
		if (!g_Game || !g_Game.IsDedicatedServer())
			return;

		if (REPORT_PRINTED)
			return;

		REPORT_PRINTED = true;
		CheckDirectories();

		string logFilePath = LOG_DIR + "/RAID_INFO_" + GetCurrentDateAndTimePathFriendly() + ".log";
		LOG_FILE = OpenFile(logFilePath, FileMode.WRITE);
		if (LOG_FILE == 0)
		{
			Print("[ZenRaidBalance] Failed to open log file: " + logFilePath);
			return;
		}

		map<int, ref ZenRaidBalanceTerritoryStats> territories = new map<int, ref ZenRaidBalanceTerritoryStats>;
		ZenRaidBalanceTerritoryStats outsideTerritories = new ZenRaidBalanceTerritoryStats(-1, "NO TERRITORY / UNASSIGNED", "0 0 0");
		ZenRaidBalanceTerritoryStats storedOutsideTerritories = new ZenRaidBalanceTerritoryStats(-2, "STORED OUTSIDE TERRITORIES", "0 0 0");
		ExpansionTerritoryModule territoryModule = GetTerritoryModule();

		PreloadTerritories(territoryModule, territories);

		int trackedItemCount = 0;
		int chargeTotal = 0;
		int chargeOutputTotal = 0;
		int storedChargeTotal = 0;
		int storedChargeOutputTotal = 0;

		foreach (ItemBase item : ZenRaidBalanceRegistry.TRACKED_ITEMS)
		{
			if (!item)
				continue;

			trackedItemCount++;

			int chargeDamage;
			string chargeType;
			if (GetChargeDamage(item, chargeDamage, chargeType))
			{
				chargeTotal++;
				chargeOutputTotal += chargeDamage;

				EntityAI storageRoot = GetStoredRoot(item);
				if (storageRoot)
				{
					ExpansionTerritory storageTerritory = FindTerritoryForPosition(territoryModule, storageRoot.GetPosition());
					if (storageTerritory)
					{
						ZenRaidBalanceTerritoryStats storageStats = GetOrCreateTerritoryStats(territories, storageTerritory);
						storageStats.AddOutput(chargeType, chargeDamage);
						storedChargeTotal++;
						storedChargeOutputTotal += chargeDamage;
					}
					else
					{
						storedOutsideTerritories.AddOutput(chargeType, chargeDamage);
					}
				}

				continue;
			}

			BaseBuildingBase baseObject = BaseBuildingBase.Cast(item);
			if (baseObject)
			{
				ExpansionTerritory territory = FindTerritoryForPosition(territoryModule, baseObject.GetPosition());
				ZenRaidBalanceTerritoryStats targetStats = outsideTerritories;
				if (territory)
					targetStats = GetOrCreateTerritoryStats(territories, territory);

				AddRaidRequirementForBaseObject(baseObject, targetStats);
			}
		}

		array<ref ZenRaidBalanceTerritoryStats> sortedTerritories = BuildSortedTerritoryArray(territories);
		int territoriesWithRequired = CountTerritoriesWithRequired(sortedTerritories);
		int territoriesWithStoredOutput = CountTerritoriesWithStoredOutput(sortedTerritories);

		int globalRaidDamageRequiredInsideTerritories = GetTotalRequired(sortedTerritories);
		int globalRaidDamageRequiredTotal = globalRaidDamageRequiredInsideTerritories + outsideTerritories.RaidDamageRequired;
		int globalRaidDamageAvailableTotal = chargeOutputTotal;
		int globalRaidDamageStoredInsideTerritories = storedChargeOutputTotal;

		int globalAvailableCoverage = 0;
		if (globalRaidDamageRequiredTotal > 0)
			globalAvailableCoverage = Math.Round((globalRaidDamageAvailableTotal * 100.0) / globalRaidDamageRequiredTotal);

		int globalStoredTerritoryCoverage = 0;
		if (globalRaidDamageRequiredTotal > 0)
			globalStoredTerritoryCoverage = Math.Round((globalRaidDamageStoredInsideTerritories * 100.0) / globalRaidDamageRequiredTotal);

		float globalAvailableToRequiredRatio = 0.0;
		if (globalRaidDamageRequiredTotal > 0)
			globalAvailableToRequiredRatio = (globalRaidDamageAvailableTotal * 1.0) / globalRaidDamageRequiredTotal;

		LogLine("============================================================");
		LogLine("ZEN RAID BALANCE REPORT - " + GetCurrentDateAndTime());
		LogLine("File: " + logFilePath);
		LogLine("============================================================");
		LogLine("Tracked loaded items: " + trackedItemCount.ToString());
		LogLine("Expansion territories found: " + sortedTerritories.Count().ToString());
		LogLine("Territories with raid requirements: " + territoriesWithRequired.ToString());
		LogLine("Territories with stored raid output: " + territoriesWithStoredOutput.ToString());
		LogLine("---");
		LogLine("GLOBAL RAID BALANCE");
		LogLine("Server Raid Ratio (total damage vs total base strength): " + globalRaidDamageAvailableTotal.ToString() + "/" + globalRaidDamageRequiredTotal.ToString() + " = " + globalAvailableToRequiredRatio.ToString() + "x");
		LogLine("Total raid damage available coverage: " + globalAvailableCoverage.ToString() + "%");
		LogLine("Stored-in-territories raid damage coverage: " + globalStoredTerritoryCoverage.ToString() + "%");
		LogLine("---");
		LogLine("Raid damage required inside territories: " + globalRaidDamageRequiredInsideTerritories.ToString());
		LogLine("Raid damage required outside territories: " + outsideTerritories.RaidDamageRequired.ToString());
		LogLine("Raid damage required total: " + globalRaidDamageRequiredTotal.ToString());
		LogLine("Breaching charges anywhere: count=" + chargeTotal.ToString() + " output=" + globalRaidDamageAvailableTotal.ToString());
		LogLine("Breaching charges stored inside territories: count=" + storedChargeTotal.ToString() + " output=" + globalRaidDamageStoredInsideTerritories.ToString());
		LogLine("---");
		LogLine("Territories are sorted by highest raid damage required.");
		LogLine("Coverage means stored raid output divided by raid damage required.");
		LogLine("============================================================");

		for (int i = 0; i < sortedTerritories.Count(); i++)
		{
			ZenRaidBalanceTerritoryStats stats = sortedTerritories[i];
			if (!PRINT_EMPTY_TERRITORIES && stats.RaidDamageRequired <= 0 && stats.RaidDamageOutputStored <= 0)
				continue;

			PrintTerritoryStats(stats, i + 1);
		}

		if (outsideTerritories.RaidDamageRequired > 0)
		{
			LogLine("============================================================");
			LogLine("BASEBUILDING OUTSIDE EXPANSION TERRITORIES");
			PrintTerritoryStats(outsideTerritories, -1);
		}

		if (storedOutsideTerritories.RaidDamageOutputStored > 0)
		{
			LogLine("============================================================");
			LogLine("STORED BREACHING CHARGES OUTSIDE EXPANSION TERRITORIES");
			PrintTerritoryStats(storedOutsideTerritories, -1);
		}

		LogLine("============================================================");
		LogLine("END RAID BALANCE REPORT");
		LogLine("============================================================");
		CloseFile(LOG_FILE);
		Print("[ZenRaidBalance] Wrote raid balance report to " + logFilePath);
	}

	static void AddRaidRequirementForBaseObject(BaseBuildingBase baseObject, ZenRaidBalanceTerritoryStats stats)
	{
		if (!baseObject || !stats)
			return;

		HDSN_DestructionManager destructionManager = HDSN_DestructionManager.GetInstance();
		HDSN_BreachingChargeConfigManager configManager = HDSN_BreachingChargeConfigManager.GetInstance();
		if (!destructionManager || !configManager)
			return;

		if (baseObject.GetType() == "Watchtower")
		{
			for (int level = 1; level <= 3; level++)
			{
				for (int wall = 1; wall <= 3; wall++)
				{
					if (!destructionManager.IsWallBuild(baseObject, level, wall))
						continue;

					string watchtowerWallType = destructionManager.GetWallType(baseObject, level, wall);
					int watchtowerWallDamage = GetTierDamageForType(configManager, watchtowerWallType);
					if (watchtowerWallDamage <= 0)
						continue;

					Watchtower watchtower = Watchtower.Cast(baseObject);
					if (watchtower)
					{
						int currentWallHealth = watchtower.GetWatchtowerWallHealthHDSN(level, wall);
						if (currentWallHealth > 0)
							watchtowerWallDamage = currentWallHealth;
					}

					stats.AddRequired(watchtowerWallType, watchtowerWallDamage);
				}
			}

			return;
		}

		string objectRaidType = destructionManager.GetWallType(baseObject);
		int objectDamage = GetTierDamageForType(configManager, objectRaidType);
		if (objectDamage <= 0)
			return;

		int currentHealth = baseObject.GetHealthHDSN();
		if (currentHealth > 0)
			objectDamage = currentHealth;

		stats.AddRequired(objectRaidType, objectDamage);
	}

	static int GetTierDamageForType(HDSN_BreachingChargeConfigManager configManager, string raidType)
	{
		if (!configManager || raidType == "")
			return 0;

		HDSN_TierParams tierParams;
		if (!configManager.FindTierParamsByObjectName(raidType, tierParams))
			return 0;

		return tierParams.GetObjectHealth();
	}

	static bool GetChargeDamage(ItemBase item, out int damage, out string chargeType)
	{
		damage = 0;
		chargeType = "";

		HDSN_BreachingChargeBase charge = HDSN_BreachingChargeBase.Cast(item);
		if (!charge)
			return false;

		chargeType = charge.GetType();
		HDSN_ChargeParams chargeParams;
		if (!HDSN_BreachingChargeConfigManager.GetInstance().FindChargeParams(chargeType, chargeParams))
			return false;

		damage = chargeParams.DamageToObjects;
		if (damage <= 0)
			return false;

		return true;
	}

	static EntityAI GetStoredRoot(ItemBase item)
	{
		if (!item)
			return null;

		EntityAI parent = item.GetHierarchyParent();
		if (!parent)
			return null;

		EntityAI root = EntityAI.Cast(item);
		while (parent)
		{
			root = parent;
			parent = root.GetHierarchyParent();
		}

		if (PlayerBase.Cast(root))
			return null;

		return root;
	}

	static ExpansionTerritoryModule GetTerritoryModule()
	{
		ExpansionTerritoryModule module;
		CF_Modules<ExpansionTerritoryModule>.Get(module);
		return module;
	}

	static ExpansionTerritory FindTerritoryForPosition(ExpansionTerritoryModule module, vector position)
	{
		if (!module)
			return null;

		TerritoryFlag flag = module.GetFlagAtPosition3D(position);
		if (!flag)
			return null;

		return flag.GetTerritory();
	}

	static void PreloadTerritories(ExpansionTerritoryModule module, map<int, ref ZenRaidBalanceTerritoryStats> territories)
	{
		if (!module || !territories)
			return;

		map<int, ref ExpansionTerritory> expansionTerritories = module.GetTerritories();
		if (!expansionTerritories)
			return;

		foreach (int id, ExpansionTerritory territory : expansionTerritories)
		{
			if (territory)
				GetOrCreateTerritoryStats(territories, territory);
		}
	}

	static ZenRaidBalanceTerritoryStats GetOrCreateTerritoryStats(map<int, ref ZenRaidBalanceTerritoryStats> territories, ExpansionTerritory territory)
	{
		if (!territory)
			return null;

		int id = territory.GetTerritoryID();
		ZenRaidBalanceTerritoryStats stats;
		if (territories.Find(id, stats))
			return stats;

		stats = new ZenRaidBalanceTerritoryStats(id, territory.GetTerritoryName(), territory.GetPosition());
		territories.Insert(id, stats);
		return stats;
	}

	static array<ref ZenRaidBalanceTerritoryStats> BuildSortedTerritoryArray(map<int, ref ZenRaidBalanceTerritoryStats> territories)
	{
		array<ref ZenRaidBalanceTerritoryStats> result = new array<ref ZenRaidBalanceTerritoryStats>;
		if (!territories)
			return result;

		for (int i = 0; i < territories.Count(); i++)
		{
			int key = territories.GetKey(i);
			ZenRaidBalanceTerritoryStats stats;
			if (territories.Find(key, stats) && stats)
				result.Insert(stats);
		}

		SortTerritories(result);
		return result;
	}

	static void SortTerritories(array<ref ZenRaidBalanceTerritoryStats> territories)
	{
		for (int i = 0; i < territories.Count() - 1; i++)
		{
			for (int j = i + 1; j < territories.Count(); j++)
			{
				if (territories[j].RaidDamageRequired > territories[i].RaidDamageRequired)
				{
					ZenRaidBalanceTerritoryStats temp = territories[i];
					territories[i] = territories[j];
					territories[j] = temp;
				}
			}
		}
	}

	static int GetTotalRequired(array<ref ZenRaidBalanceTerritoryStats> territories)
	{
		int total = 0;
		for (int i = 0; i < territories.Count(); i++)
			total += territories[i].RaidDamageRequired;

		return total;
	}

	static int CountTerritoriesWithRequired(array<ref ZenRaidBalanceTerritoryStats> territories)
	{
		int total = 0;
		for (int i = 0; i < territories.Count(); i++)
		{
			if (territories[i].RaidDamageRequired > 0)
				total++;
		}

		return total;
	}

	static int CountTerritoriesWithStoredOutput(array<ref ZenRaidBalanceTerritoryStats> territories)
	{
		int total = 0;
		for (int i = 0; i < territories.Count(); i++)
		{
			if (territories[i].RaidDamageOutputStored > 0)
				total++;
		}

		return total;
	}

	static void PrintTerritoryStats(ZenRaidBalanceTerritoryStats stats, int rank)
	{
		if (!stats)
			return;

		string prefix = "[" + rank.ToString() + "]";
		if (rank < 0)
			prefix = "[-]";

		int coverage = 0;
		if (stats.RaidDamageRequired > 0)
			coverage = Math.Round((stats.RaidDamageOutputStored * 100.0) / stats.RaidDamageRequired);

		LogLine("---");
		LogLine(prefix + " Territory: " + stats.TerritoryName + " | ID=" + stats.TerritoryID.ToString() + " | Pos=" + stats.TerritoryPosition.ToString());
		LogLine("Raid Damage Required: " + stats.RaidDamageRequired.ToString() + " | Stored Raid Output: " + stats.RaidDamageOutputStored.ToString() + " | Coverage: " + coverage.ToString() + "%");
		LogLine("Raid Targets: " + stats.RaidTargetCount.ToString() + " | Stored Charges: " + stats.StoredChargeCount.ToString());

		if (stats.RequiredByType.Count() > 0)
		{
			LogLine("Required by target type:");
			PrintBreakdown(stats.RequiredByType);
		}

		if (stats.OutputByType.Count() > 0)
		{
			LogLine("Stored output by charge type:");
			PrintBreakdown(stats.OutputByType);
		}
	}

	static void PrintBreakdown(map<string, ref ZenRaidBalanceBreakdown> breakdownMap)
	{
		int printed = 0;
		for (int i = 0; i < breakdownMap.Count(); i++)
		{
			if (printed >= MAX_BREAKDOWN_LINES)
			{
				LogLine("  ... " + (breakdownMap.Count() - printed).ToString() + " more types hidden");
				return;
			}

			string key = breakdownMap.GetKey(i);
			ZenRaidBalanceBreakdown breakdown;
			if (breakdownMap.Find(key, breakdown) && breakdown)
			{
				LogLine("  " + breakdown.Type + " count=" + breakdown.Count.ToString() + " damage=" + breakdown.Damage.ToString());
				printed++;
			}
		}
	}

	static void LogLine(string message)
	{
		Print("[ZenRaidBalance] " + message);
		if (LOG_FILE != 0)
			FPrintln(LOG_FILE, message);
	}

	static void CheckDirectories()
	{
		if (!FileExist(LOG_ROOT))
			MakeDirectory(LOG_ROOT);

		if (!FileExist(LOG_DIR))
			MakeDirectory(LOG_DIR);
	}

	static string GetCurrentTime()
	{
		int hours, minutes, seconds;
		string h, m, s;
		GetHourMinuteSecond(hours, minutes, seconds);
		h = hours.ToString();
		m = minutes.ToString();
		s = seconds.ToString();
		if (hours < 10)
			h = "0" + h;

		if (minutes < 10)
			m = "0" + m;

		if (seconds < 10)
			s = "0" + s;

		return h + "-" + m + "-" + s;
	}

	static string GetCurrentDate()
	{
		int days, months, years;
		string d, m, y;
		GetYearMonthDay(years, months, days);
		d = days.ToString();
		m = months.ToString();
		y = years.ToString();
		if (days < 10)
			d = "0" + d;

		if (months < 10)
			m = "0" + m;

		if (years < 10)
			y = "0" + y;

		return d + "-" + m + "-" + y;
	}

	static string GetCurrentDateAndTimePathFriendly()
	{
		return GetCurrentDate() + "_" + GetCurrentTime();
	}

	static string GetCurrentDateAndTime()
	{
		string date = GetCurrentDate();
		string time = GetCurrentTime();
		date.Replace("-", "/");
		time.Replace("-", ":");
		return date + " " + time;
	}
}
