modded class ItemBase
{
#ifdef SERVER
	override void EOnInit(IEntity other, int extra)
	{
		super.EOnInit(other, extra);
		ZenRaidBalanceRegistry.Register(this);
	}

	override void EEDelete(EntityAI parent)
	{
		super.EEDelete(parent);
		ZenRaidBalanceRegistry.Unregister(this);
	}
#endif
}
