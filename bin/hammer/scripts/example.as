
class ExampleWedge : ScriptSolid
{
	void SetData( const BoundBox@ box )
	{
		width = (box.maxs[0] - box.mins[0]) / 2;
		depth = (box.maxs[1] - box.mins[1]) / 2;
		height = (box.maxs[2] - box.mins[2]) / 2;

		box.GetBoundsCenter(origin);
	}

	CMapClass@ CreateMapSolid( TextureAlignment_t align )
	{
		CMapFace face;
		CMapSolid@ solid = CreateSolid();
		Vector[] points;
		points.resize(4);

		points[0][0] = origin[0] + width;
		points[0][1] = origin[1] + depth;
		points[0][2] = origin[2] + height;

		points[1][0] = origin[0] + width;
		points[1][1] = origin[1] - depth;
		points[1][2] = origin[2] + height;

		points[2][0] = origin[0] - width;
		points[2][1] = origin[1] - depth;
		points[2][2] = origin[2] + height;

		face.CreateFace(points, 3 );
		solid.AddFace(face);

		for (int i = 0; i < 3; i++)
		{
			points[i][2] = origin[2] - height;
		}

		face.CreateFace(points, -3 );
		solid.AddFace(face);

		points[0][0] = origin[0] + width;
		points[0][1] = origin[1] + depth;
		points[0][2] = origin[2] - height;

		points[1][0] = origin[0] + width;
		points[1][1] = origin[1] + depth;
		points[1][2] = origin[2] + height;

		points[2][0] = origin[0] - width;
		points[2][1] = origin[1] - depth;
		points[2][2] = origin[2] + height;

		points[3][0] = origin[0] - width;
		points[3][1] = origin[1] - depth;
		points[3][2] = origin[2] - height;

		face.CreateFace(points, 4 );
		solid.AddFace(face);

		points[0][0] = origin[0] + width;
		points[0][1] = origin[1] - depth;
		points[0][2] = origin[2] + height;

		points[1][0] = origin[0] + width;
		points[1][1] = origin[1] - depth;
		points[1][2] = origin[2] - height;

		points[2][0] = origin[0] - width;
		points[2][1] = origin[1] - depth;
		points[2][2] = origin[2] - height;

		points[3][0] = origin[0] - width;
		points[3][1] = origin[1] - depth;
		points[3][2] = origin[2] + height;

		face.CreateFace(points, 4 );
		solid.AddFace(face);

		points[0][0] = origin[0] + width;
		points[0][1] = origin[1] + depth;
		points[0][2] = origin[2] + height;

		points[1][0] = origin[0] + width;
		points[1][1] = origin[1] + depth;
		points[1][2] = origin[2] - height;

		points[2][0] = origin[0] + width;
		points[2][1] = origin[1] - depth;
		points[2][2] = origin[2] - height;

		points[3][0] = origin[0] + width;
		points[3][1] = origin[1] - depth;
		points[3][2] = origin[2] + height;

		face.CreateFace(points, 4 );
		solid.AddFace(face);

		solid.CalcBounds(false);
		solid.InitializeTextureAxes(align, INIT_TEXTURE_ALL | INIT_TEXTURE_FORCE);

		return solid;
	}

	Vector origin;
	float width;
	float depth;
	float height;
};

void RegisterCallback()
{
	RegisterScriptSolid("script_wedge", ExampleWedge());
}